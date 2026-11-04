"""Offline Zen reconciliation regression checks. No macOS desktop required."""
from __future__ import annotations

import importlib.util
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "qualify_zen_reconciliation.py"
SPEC = importlib.util.spec_from_file_location("qualify_zen_reconciliation", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)
BINARY = Path("/tmp/yabai-test")


def tree(apps):
    return {"type": "leaf", "apps": apps, "titles": [""] * len(apps)}


def snapshot(ids=(11,), *, space_index=3, space_id=500, label=None,
             layout_apps=None, extra=None, enabled="on", floating=(), missing_ax=(),
             space_members=None, focused=True):
    ids = list(ids)
    wins = [{"id": wid, "app": "Zen", "space": space_index,
             "has-ax-reference": wid not in missing_ax,
             "is-floating": wid in floating}
            for wid in ids]
    wins.extend(extra or [])
    return {
        "reconciliation": enabled, "interval": 750,
        "spaces": [{"id": space_id, "index": space_index,
                    "label": label if label is not None else module.SPACE_LABEL,
                    "type": "bsp", "has-focus": focused,
                    "windows": space_members if space_members is not None else ids}],
        "windows": wins,
        "layout": {"layout": "bsp", "tree": tree(
            layout_apps if layout_apps is not None else ["Zen"] * len(ids)
        )},
    }


def advance(phase, state, ids, **kwargs):
    return module.advance(phase, state, snapshot(ids, **kwargs), BINARY, 3, 500)


def test_complete_staged_qualification():
    state = advance("baseline", None, [11])
    state = advance("ordinary", state, [11, 12])
    assert state["ordinary"] == 12
    state = advance("detach1", state, [11, 12, 13])
    state = advance("detach2", state, [11, 12, 13, 14])
    state = advance("returned", state, [11, 12, 13, 14])
    state = advance("closed", state, [11, 12])
    assert state["phase"] == "closed"


@pytest.mark.parametrize(("kwargs", "match"), [
    ({"missing_ax": (12,)}, "missing AX reference"),
    ({"floating": (12,)}, "floating"),
    ({"space_members": [11]}, "Space membership"),
    ({"layout_apps": ["Zen"]}, "BSP tree"),
    ({"label": "real-work-space"}, "labeled"),
    ({"focused": False}, "active and BSP"),
    ({"enabled": "off"}, "not enabled"),
])
def test_refuses_invalid_registration_or_preconditions(kwargs, match):
    state = advance("baseline", None, [11])
    with pytest.raises(module.QualificationError, match=match):
        advance("ordinary", state, [11, 12], **kwargs)


def test_duplicated_window_ids_are_rejected():
    bad = snapshot([11, 12], extra=[{"id": 12, "app": "Kitty"}])
    with pytest.raises(module.QualificationError, match="duplicate"):
        module.observed_zen_ids(bad, 3, 500)


def test_window_stays_on_exact_space_id():
    state = advance("baseline", None, [11])
    with pytest.raises(module.QualificationError, match="ID changed"):
        advance("ordinary", state, [11, 12], space_id=501)


def test_new_window_must_be_unique():
    state = advance("baseline", None, [11])
    with pytest.raises(module.QualificationError, match="exactly one"):
        advance("ordinary", state, [11, 12, 13])


def test_existing_window_must_survive():
    state = advance("baseline", None, [11])
    with pytest.raises(module.QualificationError, match="missing"):
        advance("ordinary", state, [12])


def test_no_skipped_stage():
    state = advance("baseline", None, [11])
    with pytest.raises(module.QualificationError, match="previous phase"):
        advance("detach1", state, [11, 12])


def test_detached_windows_must_disappear_on_close():
    state = advance("baseline", None, [11])
    state = advance("ordinary", state, [11, 12])
    state = advance("detach1", state, [11, 12, 13])
    state = advance("detach2", state, [11, 12, 13, 14])
    state = advance("returned", state, [11, 12, 13, 14])
    with pytest.raises(module.QualificationError, match="not fully removed"):
        advance("closed", state, [11, 12, 13])


def test_source_wiring_default_and_dedup():
    source = (ROOT / "src" / "message.c").read_text()
    handler = (ROOT / "src" / "event_loop.c").read_text()
    assert "static bool g_window_reconciliation_enabled = true;" in source
    startup = source.split("bool message_loop_begin(char *socket_path)", 1)[1]
    assert "window_reconciliation_update_timer();" in startup
    tick = source.split("void window_reconciliation_tick(void)", 1)[1].split(
        "static void refresh_untracked_windows", 1
    )[0]
    assert "g_window_reconciliation_enabled" in tick
    assert 'string_equals(application->name, "Zen")' in tick
    assert "!application->is_hidden" in tick
    assert "window_manager_find_window(&g_window_manager, wid)" in source
    assert "WINDOW_CREATED" in tick or "queue_missing_application_windows" in tick
    assert "if (existing_window) { CFRelease(context); return; }" in handler
    assert "window_manager_should_manage_window(window)" in handler
    assert "space_manager_tile_window_on_space" in handler
