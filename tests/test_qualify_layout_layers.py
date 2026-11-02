"""Read-only layer audit, source-wiring, and guarded mutation checks."""
import importlib.util
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "qualify_layout_layers.py"
SPEC = importlib.util.spec_from_file_location("qualify_layout_layers", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)
BINARY = Path("/tmp/yabai")


@pytest.fixture
def cycle_inputs():
    space = {"id": 50, "index": 4, "type": "bsp", "has-focus": True}
    window = {
        "id": 77, "space": 4, "is-floating": False,
        "layer": "normal", "sub-layer": "below", "sub-level": -20,
    }
    return space, window


def test_flags_below_sublevel_on_float_space_only():
    spaces = [
        {"id": 4, "index": 3, "type": "float"},
        {"id": 50, "index": 4, "type": "bsp"},
    ]
    windows = [
        {"id": 106, "space": 3, "app": "kitty",
         "layer": "normal", "sub-layer": "below", "sub-level": -20},
        {"id": 1934, "space": 3, "app": "Zen",
         "layer": "normal", "sub-layer": "normal", "sub-level": 0},
        {"id": 1213, "space": 4, "app": "kitty",
         "layer": "normal", "sub-layer": "below", "sub-level": -20},
        {"id": 500, "space": 3, "app": "utility",
         "layer": "above", "sub-layer": "below", "sub-level": -20},
    ]
    assert module.float_space_anomalies(spaces, windows) == [
        {"space": 3, "id": 106, "app": "kitty", "sub_level": -20}
    ]


def test_candidates_exclude_floats_and_unlowered():
    windows = [
        {"id": 1, "space": 4, "layer": "normal",
         "sub-layer": "below", "sub-level": -20, "is-floating": False},
        {"id": 2, "space": 4, "layer": "normal",
         "sub-layer": "below", "sub-level": -20, "is-floating": True},
        {"id": 3, "space": 4, "layer": "normal",
         "sub-layer": "normal", "sub-level": 0, "is-floating": False},
        {"id": 4, "space": 3, "layer": "normal",
         "sub-layer": "below", "sub-level": -20, "is-floating": False},
    ]
    assert module.managed_candidates(windows, 4) == {1}


def test_missing_window_is_reported():
    assert module.layer_results(
        [{"id": 3, "sub-level": 0, "sub-layer": "normal"}], {3, 4}
    ) == {
        3: {"present": True, "sub_level": 0, "sub_layer": "normal"},
        4: {"present": False, "sub_level": None, "sub_layer": None},
    }


def _stub_cycle(monkeypatch, snapshots, *, command_error=None):
    """Capture calls while keeping every subprocess and macOS effect mocked."""
    calls = []
    preflight = []
    iterator = iter(snapshots)

    def command(binary, *args):
        calls.append((binary, *args))
        if command_error is not None and len(calls) == 2:
            raise command_error
        return ""

    def snapshot(binary):
        result = next(iterator)
        if isinstance(result, Exception):
            raise result
        return result

    def subprocess_run(*args, **kwargs):
        preflight.append((args, kwargs))
        return None

    monkeypatch.setattr(module.subprocess, "run", subprocess_run)
    monkeypatch.setattr(module, "command", command)
    monkeypatch.setattr(module, "snapshot", snapshot)
    monkeypatch.setattr(module.time, "sleep", lambda seconds: None)
    return calls, preflight


def test_cycle_restores_original_layout_and_checks_layers(monkeypatch, cycle_inputs, capsys):
    space, window = cycle_inputs
    midspace = dict(space, type="float")
    midwindow = dict(window, **{"sub-level": 0, "sub-layer": "normal"})
    calls, preflight = _stub_cycle(monkeypatch, [
        ([space], [window]),
        ([midspace], [midwindow]),
        ([space], [window]),
    ])
    assert module.qualify_cycle(BINARY, 4, 1.0) == 0
    assert len(preflight) == 1
    assert calls == [
        (BINARY, "--check-sa"),
        (BINARY, "-m", "space", "4", "--layout", "float"),
        (BINARY, "-m", "space", "4", "--layout", "bsp"),
    ]
    assert '"float_layer_pass": true' in capsys.readouterr().out


def test_exception_during_float_probe_still_restores(monkeypatch, cycle_inputs):
    space, window = cycle_inputs
    calls, _ = _stub_cycle(monkeypatch, [
        ([space], [window]), ValueError("bad query"),
    ])
    with pytest.raises(ValueError, match="bad query"):
        module.qualify_cycle(BINARY, 4, 1.0)
    assert calls[-1] == (BINARY, "-m", "space", "4", "--layout", "bsp")


def test_float_command_failure_still_attempts_restoration(monkeypatch, cycle_inputs):
    space, window = cycle_inputs
    calls, _ = _stub_cycle(
        monkeypatch, [([space], [window])],
        command_error=RuntimeError("IPC interrupted"),
    )
    with pytest.raises(RuntimeError, match="IPC interrupted"):
        module.qualify_cycle(BINARY, 4, 1.0)
    assert calls[-1] == (BINARY, "-m", "space", "4", "--layout", "bsp")


def test_refuses_nonfocused_space_without_mutating(monkeypatch, cycle_inputs):
    space, window = cycle_inputs
    space = dict(space, **{"has-focus": False})
    calls, _ = _stub_cycle(monkeypatch, [([space], [window])])
    with pytest.raises(RuntimeError, match="active"):
        module.qualify_cycle(BINARY, 4, 1.0)
    assert calls == [(BINARY, "--check-sa")]


def test_qualification_config_contains_no_executable_commands():
    config = (ROOT / "scripts" / "qualification.yabairc").read_text()
    executable_lines = [
        line for line in config.splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    assert executable_lines == []


def test_just_run_does_not_load_personal_config():
    justfile = (ROOT / "justfile").read_text()
    block = justfile.split("\nrun: qualify-preflight\n", 1)[1].split("\n\n", 1)[0]
    assert "qualification_config" in block
    assert "config_file" not in block
    assert "run-user-config: qualify-preflight" in justfile


def test_restore_precedes_tree_clear():
    source = (ROOT / "src" / "space_manager.c").read_text()
    body = source.split("void space_manager_set_layout_for_space(", 1)[1].split(
        "\n}\n", 1
    )[0]
    assert body.index("space_manager_restore_layers_before_float(view)") < body.index(
        "view_clear(view)"
    )


def test_global_and_scoped_config_use_shared_setter():
    space_src = (ROOT / "src" / "space_manager.c").read_text()
    message_src = (ROOT / "src" / "message.c").read_text()
    assert "space_manager_set_layout_for_space(sm, view->sid, layout)" in space_src
    for layout in ("VIEW_BSP", "VIEW_STACK", "VIEW_FLOAT"):
        assert (
            f"space_manager_set_layout_for_space(&g_space_manager, sel_sid, {layout})"
            in message_src
        )
