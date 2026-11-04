#!/usr/bin/env python3
"""Read-only, phase-bounded WindowServer invalid-window diagnostics on macOS.

No yabai IPC, layout mutation, Dock changes, privileged actions, or daemon
lifecycle management. The operator controls every desktop interaction.
"""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timedelta
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

DEFAULT_DIR = Path("/tmp/yabai-windowserver-diagnostics")
ERROR = "_CGXPackagesSetWindowConstraints: Invalid window"
PREDICATE = (
    'process == "WindowServer" AND '
    'eventMessage CONTAINS "_CGXPackagesSetWindowConstraints"'
)
EVENT_RE = re.compile(
    r"^(?P<time>\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+)"
    r".*?\bWindowServer\[\d+:[^\]]+\].*?"
    r"_CGXPackagesSetWindowConstraints: Invalid window\s*$"
)
LABEL_RE = re.compile(r"[a-z][a-z0-9_-]{0,63}\Z")


class DiagnosticError(RuntimeError):
    pass


def valid_label(label: str) -> str:
    if not LABEL_RE.fullmatch(label):
        raise DiagnosticError(
            "phase label must start with a lowercase letter and contain "
            "only lowercase letters, numbers, hyphens or underscores (max 64)"
        )
    return label


def parse_events(text: str) -> list[datetime]:
    """Accept only the exact target error from WindowServer, not other logs."""
    times = []
    for line in text.splitlines():
        match = EVENT_RE.match(line)
        if match:
            times.append(datetime.fromisoformat(match.group("time")))
    return sorted(times)


def summarize(events: list[datetime], *, max_gap_seconds: float = 1.0) -> dict:
    """Group subsecond bursts; do not infer their initiating process."""
    if max_gap_seconds <= 0:
        raise ValueError("max_gap_seconds must be positive")
    bursts: list[dict] = []
    grouped: list[datetime] = []
    for instant in sorted(events):
        if grouped and (instant - grouped[-1]).total_seconds() > max_gap_seconds:
            bursts.append(_burst(grouped))
            grouped = []
        grouped.append(instant)
    if grouped:
        bursts.append(_burst(grouped))
    counts = Counter(t.strftime("%H:%M:%S") for t in events)
    sizes = Counter(b["count"] for b in bursts)
    return {
        "error_count": len(events),
        "burst_count": len(bursts),
        "burst_size_histogram": {str(k): v for k, v in sorted(sizes.items())},
        "counts_by_second": dict(sorted(counts.items())),
        "bursts": bursts,
    }


def _burst(times: list[datetime]) -> dict:
    return {
        "start": times[0].isoformat(timespec="milliseconds"),
        "end": times[-1].isoformat(timespec="milliseconds"),
        "count": len(times),
        "duration_ms": round((times[-1] - times[0]).total_seconds() * 1000, 3),
    }


def yabai_pids() -> list[int]:
    result = subprocess.run(
        ["pgrep", "-x", "yabai"], capture_output=True, text=True,
        check=False, timeout=10,
    )
    if result.returncode == 1:
        return []
    if result.returncode:
        raise DiagnosticError(f"pgrep failed: {result.stderr.strip()}")
    return sorted(int(pid) for pid in result.stdout.split())


def local_now() -> datetime:
    return datetime.now().astimezone()


def _file_set(directory: Path, label: str) -> tuple[Path, Path, Path]:
    valid_label(label)
    return (
        directory / f"{label}.active.json",
        directory / f"{label}.json",
        directory / f"{label}.log",
    )


def begin(directory: Path, label: str) -> dict:
    active, report, raw = _file_set(directory, label)
    if report.exists() or raw.exists():
        raise DiagnosticError(f"phase {label!r} already completed; use a new label")
    directory.mkdir(parents=True, exist_ok=True)
    state = {
        "phase": label,
        "started": local_now().isoformat(timespec="microseconds"),
        "yabai_pids_before": yabai_pids(),
    }
    try:
        # Exclusive creation prevents accidentally overwriting a live phase.
        with active.open("x", encoding="utf-8") as file:
            os.chmod(active, 0o600)
            json.dump(state, file, indent=2, sort_keys=True)
            file.write("\n")
    except FileExistsError as exc:
        raise DiagnosticError(
            f"phase {label!r} already active; finish it before starting again"
        ) from exc
    return state


def retrieve_log(started: datetime, ended: datetime) -> str:
    # log show accepts local date/time at whole-second precision. Widen the
    # query by one second, then filter against precise boundaries ourselves.
    lower = (started - timedelta(seconds=1)).strftime("%Y-%m-%d %H:%M:%S")
    upper = (ended + timedelta(seconds=1)).strftime("%Y-%m-%d %H:%M:%S")
    result = subprocess.run(
        ["log", "show", "--style", "compact",
         "--start", lower, "--end", upper, "--predicate", PREDICATE],
        capture_output=True, text=True, check=False, timeout=60,
    )
    if result.returncode:
        raise DiagnosticError(
            f"log show exited {result.returncode}: {result.stderr.strip()}"
        )
    return result.stdout


def finish(directory: Path, label: str) -> dict:
    active, report, raw = _file_set(directory, label)
    if not active.is_file():
        raise DiagnosticError(f"no active phase {label!r}; run start first")
    state = json.loads(active.read_text(encoding="utf-8"))
    if state.get("phase") != label:
        raise DiagnosticError("active phase identity does not match")
    started = datetime.fromisoformat(state["started"])
    ended = local_now()
    if ended <= started:
        raise DiagnosticError("end time is not after phase start")
    if ended - started > timedelta(minutes=15):
        raise DiagnosticError(
            "phase exceeds 15 minutes; use a fresh phase label for a bounded run"
        )
    pids_after = yabai_pids()
    captured = retrieve_log(started, ended)
    start_local = started.replace(tzinfo=None)
    end_local = ended.replace(tzinfo=None)
    events = [t for t in parse_events(captured) if start_local <= t <= end_local]
    output = {
        **state,
        "ended": ended.isoformat(timespec="microseconds"),
        "elapsed_seconds": round((ended - started).total_seconds(), 3),
        "yabai_pids_after": pids_after,
        "pid_changed": state["yabai_pids_before"] != pids_after,
        **summarize(events),
    }
    if report.exists() or raw.exists():
        raise DiagnosticError("results already exist; refusing to overwrite")
    with raw.open("x", encoding="utf-8") as file:
        os.chmod(raw, 0o600)
        file.write(captured)
    with report.open("x", encoding="utf-8") as file:
        os.chmod(report, 0o600)
        json.dump(output, file, indent=2, sort_keys=True)
        file.write("\n")
    active.unlink()
    return output


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    for name in ("start", "finish", "idle"):
        sub = commands.add_parser(name)
        sub.add_argument("phase")
        sub.add_argument("--dir", type=Path, default=DEFAULT_DIR)
        if name == "idle":
            sub.add_argument("--seconds", type=int, default=30)
    analyze = commands.add_parser("analyze")
    analyze.add_argument("file", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.action == "analyze":
            result = summarize(parse_events(args.file.read_text(encoding="utf-8")))
        else:
            valid_label(args.phase)
            if sys.platform != "darwin":
                raise DiagnosticError("live phase capture requires macOS")
            if args.action == "start":
                result = begin(args.dir, args.phase)
            elif args.action == "finish":
                result = finish(args.dir, args.phase)
            else:
                if not 5 <= args.seconds <= 600:
                    raise DiagnosticError("--seconds must be 5..600")
                begin(args.dir, args.phase)
                time.sleep(args.seconds)
                result = finish(args.dir, args.phase)
        print(json.dumps(result, sort_keys=True))
        return 0
    except (DiagnosticError, OSError, ValueError, KeyError,
            subprocess.SubprocessError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
