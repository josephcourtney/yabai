"""Offline tests for read-only WindowServer phase and burst diagnostics."""
from __future__ import annotations

from datetime import datetime, timedelta, timezone
import importlib.util
import json
from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "diagnose_windowserver.py"
SPEC = importlib.util.spec_from_file_location("diagnose_windowserver", SCRIPT)
assert SPEC and SPEC.loader
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


def line(when: str) -> str:
    return (
        f"{when} E  WindowServer[464:ee4] [com.apple.SkyLight:default] "
        "_CGXPackagesSetWindowConstraints: Invalid window"
    )


def test_parse_target_error_only_and_exact_burst_sizes():
    log = "\n".join(
        [line(f"2026-11-04 11:30:26.{i:03d}") for i in range(59)]
        + [line(f"2026-11-04 11:30:58.{i:03d}") for i in range(6)]
        + ["2026-11-04 11:30:59.000 E other[1:1] Invalid window",
           "2026-11-04 11:30:59.100 E WindowServer[464:ee4] another error",
           "Timestamp Ty Process[PID:TID]"]
    )
    summary = module.summarize(module.parse_events(log))
    assert summary["error_count"] == 65
    assert summary["burst_count"] == 2
    assert summary["burst_size_histogram"] == {"6": 1, "59": 1}
    assert summary["counts_by_second"] == {
        "11:30:26": 59, "11:30:58": 6,
    }
    assert summary["bursts"][0]["duration_ms"] == 58


def test_adjacent_seconds_can_belong_to_one_burst():
    events = module.parse_events("\n".join([
        line("2026-11-04 11:30:26.900"),
        line("2026-11-04 11:30:27.100"),
        line("2026-11-04 11:30:28.105"),
    ]))
    s = module.summarize(events)
    assert [b["count"] for b in s["bursts"]] == [2, 1]


def test_empty_and_out_of_order_log():
    assert module.summarize([])["error_count"] == 0
    events = module.parse_events("\n".join([
        line("2026-11-04 11:30:29.001"),
        line("2026-11-04 11:30:26.001"),
    ]))
    assert module.summarize(events)["bursts"][0]["start"].endswith("26.001")


@pytest.mark.parametrize("label", ["", "slash/name", "../bypass", "WithCaps", "a b", "a" * 65])
def test_unsafe_phase_labels_rejected(label):
    with pytest.raises(module.DiagnosticError):
        module.valid_label(label)


def test_no_overwrite_active_or_completed(tmp_path, monkeypatch):
    monkeypatch.setattr(module, "yabai_pids", lambda: [])
    module.begin(tmp_path, "idle_off")
    with pytest.raises(module.DiagnosticError, match="already active"):
        module.begin(tmp_path, "idle_off")
    (tmp_path / "other.json").write_text("{}")
    with pytest.raises(module.DiagnosticError, match="already completed"):
        module.begin(tmp_path, "other")


def test_begin_finish_preserves_exact_window_without_mutation(tmp_path, monkeypatch):
    first = datetime(2026, 11, 4, 11, 30, 0, tzinfo=timezone(timedelta(hours=-5)))
    last = first + timedelta(seconds=30)
    now = iter((first, last))
    monkeypatch.setattr(module, "local_now", lambda: next(now))
    pids = iter(([111], [111]))
    monkeypatch.setattr(module, "yabai_pids", lambda: next(pids))
    captured = "\n".join([
        line("2026-11-04 11:29:59.999"),
        line("2026-11-04 11:30:00.000"),
        line("2026-11-04 11:30:15.000"),
        line("2026-11-04 11:30:30.001"),
    ])
    observed = []
    def mock_log(start, end):
        observed.append((start, end))
        return captured
    monkeypatch.setattr(module, "retrieve_log", mock_log)

    s = module.begin(tmp_path, "idle_on")
    assert s["yabai_pids_before"] == [111]
    assert (tmp_path / "idle_on.active.json").exists()
    result = module.finish(tmp_path, "idle_on")
    assert result["error_count"] == 2
    assert result["yabai_pids_after"] == [111]
    assert result["pid_changed"] is False
    assert result["elapsed_seconds"] == 30
    assert not (tmp_path / "idle_on.active.json").exists()
    assert json.loads((tmp_path / "idle_on.json").read_text()) == result
    assert (tmp_path / "idle_on.log").read_text() == captured
    assert observed == [(first, last)]


def test_daemon_pid_transition_recorded(tmp_path, monkeypatch):
    first = datetime(2026, 11, 4, 11, 30, tzinfo=timezone.utc)
    now = iter([first, first + timedelta(seconds=10)])
    monkeypatch.setattr(module, "local_now", lambda: next(now))
    pids = iter(([111], []))
    monkeypatch.setattr(module, "yabai_pids", lambda: next(pids))
    monkeypatch.setattr(module, "retrieve_log", lambda *args: "")
    module.begin(tmp_path, "stopped")
    result = module.finish(tmp_path, "stopped")
    assert result["pid_changed"] is True
    assert result["yabai_pids_after"] == []


def test_failed_log_retrieval_keeps_active_marker(tmp_path, monkeypatch):
    now = datetime(2026, 11, 4, 11, 30, tzinfo=timezone.utc)
    times = iter([now, now + timedelta(seconds=10)])
    monkeypatch.setattr(module, "local_now", lambda: next(times))
    monkeypatch.setattr(module, "yabai_pids", lambda: [])
    monkeypatch.setattr(
        module, "retrieve_log",
        lambda *args: (_ for _ in ()).throw(module.DiagnosticError("log unavailable")),
    )
    module.begin(tmp_path, "probe")
    with pytest.raises(module.DiagnosticError, match="log unavailable"):
        module.finish(tmp_path, "probe")
    assert (tmp_path / "probe.active.json").exists()
    assert not (tmp_path / "probe.json").exists()


def test_retrieve_log_is_read_only(monkeypatch):
    calls = []
    def fake_run(argv, **kwargs):
        calls.append((argv, kwargs))
        return subprocess.CompletedProcess(argv, 0, stdout="", stderr="")
    monkeypatch.setattr(module.subprocess, "run", fake_run)
    t = datetime(2026, 11, 4, 11, 30, tzinfo=timezone(timedelta(hours=-5)))
    assert module.retrieve_log(t, t + timedelta(seconds=30)) == ""
    argv, opts = calls[0]
    assert argv[0:3] == ["log", "show", "--style"]
    assert "--predicate" in argv
    assert opts["timeout"] == 60
    assert len(calls) == 1


def test_reject_long_phase(tmp_path, monkeypatch):
    t = datetime(2026, 11, 4, 11, 30, tzinfo=timezone.utc)
    now = iter([t, t + timedelta(minutes=16)])
    monkeypatch.setattr(module, "local_now", lambda: next(now))
    monkeypatch.setattr(module, "yabai_pids", lambda: [])
    monkeypatch.setattr(
        module, "retrieve_log",
        lambda *args: pytest.fail("queried logs despite long phase"),
    )
    module.begin(tmp_path, "too_long")
    with pytest.raises(module.DiagnosticError, match="exceeds 15 minutes"):
        module.finish(tmp_path, "too_long")


def test_second_phase_is_rejected_even_with_different_label(tmp_path, monkeypatch):
    monkeypatch.setattr(module, "yabai_pids", lambda: [])
    module.begin(tmp_path, "bsp_create_03")
    with pytest.raises(module.DiagnosticError, match="another phase is active"):
        module.begin(tmp_path, "bsp_create_04")
    assert sorted(module.active_labels(tmp_path)) == ["bsp_create_03"]
    assert not (tmp_path / "bsp_create_04.active.json").exists()


def test_abandon_preserves_evidence_and_allows_new_phase(tmp_path, monkeypatch):
    monkeypatch.setattr(module, "yabai_pids", lambda: [])
    module.begin(tmp_path, "old_phase")
    result = module.abandon(tmp_path, "old_phase", "Overlapping experiment")
    assert result["abandon_reason"] == "Overlapping experiment"
    assert not (tmp_path / "old_phase.active.json").exists()
    archived = tmp_path / "old_phase.abandoned.json"
    assert json.loads(archived.read_text()) == result
    with pytest.raises(module.DiagnosticError, match="was abandoned"):
        module.begin(tmp_path, "old_phase")
    with pytest.raises(module.DiagnosticError, match="no active phase"):
        module.finish(tmp_path, "old_phase")
    assert module.begin(tmp_path, "new_phase")["phase"] == "new_phase"


@pytest.mark.parametrize("reason", ["", "  ", "bad\nreason", "x" * 201])
def test_abandon_requires_usable_reason(tmp_path, monkeypatch, reason):
    monkeypatch.setattr(module, "yabai_pids", lambda: [])
    module.begin(tmp_path, "phase")
    with pytest.raises(module.DiagnosticError, match="reason"):
        module.abandon(tmp_path, "phase", reason)
    assert (tmp_path / "phase.active.json").exists()


def test_status_displays_pending_phase_without_mutation(tmp_path, monkeypatch):
    monkeypatch.setattr(module, "yabai_pids", lambda: [])
    module.begin(tmp_path, "phase")
    marker = (tmp_path / "phase.active.json").read_text()
    assert module.status(tmp_path)["active"][0]["phase"] == "phase"
    assert (tmp_path / "phase.active.json").read_text() == marker
    module.abandon(tmp_path, "phase", "Interrupted")
    assert module.status(tmp_path) == {"active": []}


def test_status_tolerates_invalid_json(tmp_path):
    (tmp_path / "broken.active.json").write_text("{broken")
    assert module.status(tmp_path) == {
        "active": [{"phase": "broken", "started": None}]
    }
