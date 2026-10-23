#!/usr/bin/env python3
"""Fail-closed macOS runtime qualification preflight for the local yabai build.

No services, processes, Dock components, or system settings are modified here.
"""
from __future__ import annotations

import argparse
import ctypes
import datetime as dt
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
CANDIDATE = ROOT / "bin" / "yabai"
WINDOW_ERROR = '_CGXPackagesSetWindowConstraints: Invalid window'
WINDOW_PREDICATE = (
    'process == "WindowServer" AND '
    'eventMessage CONTAINS "_CGXPackagesSetWindowConstraints"'
)


def command(*args: str, check: bool = True, timeout: int = 30) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args, capture_output=True, text=True, check=check, timeout=timeout
    )


def scroot_process(command_line: str) -> bool:
    """Detect live Scroot CLI, worker, menu-bar and launchagent invocations."""
    return bool(
        re.search(r"(^|[\s/])scroot(?=$|[\s./])", command_line, re.IGNORECASE)
        or "org.mund.scroot" in command_line.lower()
    )


def find_scroot() -> list[str]:
    lines = command("ps", "-ww", "-axo", "pid=,command=").stdout.splitlines()
    found = []
    for line in lines:
        parts = line.strip().split(None, 1)
        if len(parts) != 2 or not parts[0].isdigit():
            continue
        if int(parts[0]) == os.getpid():
            continue
        if scroot_process(parts[1]):
            found.append(line.strip())
    return found


def yabai_pids() -> list[int]:
    result = command("pgrep", "-x", "yabai", check=False)
    if result.returncode == 1:
        return []
    if result.returncode != 0:
        raise RuntimeError(f"pgrep failed: {result.stderr.strip()}")
    return [int(item) for item in result.stdout.split()]


def process_executable(pid: int) -> Path:
    """Identify the actual Mach-O path, not the shell command or client CLI."""
    libproc = ctypes.CDLL("/usr/lib/libproc.dylib")
    proc_pidpath = libproc.proc_pidpath
    proc_pidpath.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32]
    proc_pidpath.restype = ctypes.c_int
    buf = ctypes.create_string_buffer(4096)
    length = proc_pidpath(pid, buf, ctypes.sizeof(buf))
    if length <= 0:
        raise RuntimeError(f"cannot inspect executable for yabai PID {pid}")
    return Path(os.fsdecode(buf.value)).resolve()


def window_error_count() -> str:
    try:
        result = command(
            "log", "show", "--last", "30s", "--style", "compact",
            "--predicate", WINDOW_PREDICATE, timeout=45,
        )
        return str(sum(WINDOW_ERROR in line for line in result.stdout.splitlines()))
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
        return f"unavailable ({exc.__class__.__name__})"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("preflight", "running"))
    parser.add_argument(
        "--ref", default="", metavar="GIT_REF",
        help="Optional local ref that HEAD must match (fetch it first).",
    )
    args = parser.parse_args()

    if sys.platform != "darwin":
        parser.error("runtime qualification requires macOS")
    failures: list[str] = []

    if not CANDIDATE.is_file() or not os.access(CANDIDATE, os.X_OK):
        failures.append(f"candidate binary missing or not executable: {CANDIDATE}")
    else:
        print(f"Candidate: {CANDIDATE.resolve()}")
        print(f"Version: {command(str(CANDIDATE), '--version').stdout.strip()}")

    sha = command("git", "-C", str(ROOT), "rev-parse", "HEAD").stdout.strip()
    print(f"HEAD: {sha}")
    if args.ref:
        try:
            expected = command(
                "git", "-C", str(ROOT), "rev-parse", "--verify", args.ref
            ).stdout.strip()
        except subprocess.CalledProcessError:
            failures.append(f"cannot resolve {args.ref!r}; run git fetch origin")
        else:
            if expected != sha:
                failures.append(f"HEAD does not match {args.ref}: {expected}")

    scroot = find_scroot()
    if scroot:
        failures.append("Scroot appears to be running:\n  " + "\n  ".join(scroot))
    else:
        print("Scroot: no matching process")

    pids = yabai_pids()
    if args.mode == "preflight":
        if pids:
            failures.append(
                f"existing yabai PID(s) {pids}; stop the installed service "
                "before starting the test daemon"
            )
        else:
            print("Yabai: no running daemon (ready to start test binary)")
    else:
        if len(pids) != 1:
            failures.append(f"expected exactly one yabai daemon, found {pids}")
        else:
            exe = process_executable(pids[0])
            print(f"Running PID {pids[0]} executable: {exe}")
            if exe != CANDIDATE.resolve():
                failures.append(
                    f"wrong daemon binary: running {exe}, expected {CANDIDATE.resolve()}"
                )
            else:
                started = command(
                    "ps", "-p", str(pids[0]), "-o", "lstart="
                ).stdout.strip()
                try:
                    booted = dt.datetime.strptime(started, "%a %b %d %H:%M:%S %Y")
                    modified = dt.datetime.fromtimestamp(CANDIDATE.stat().st_mtime)
                    if modified > booted + dt.timedelta(seconds=2):
                        failures.append(
                            "candidate binary was rebuilt after the daemon started; "
                            "restart the daemon"
                        )
                except ValueError:
                    print(f"Warning: could not parse daemon start time: {started}")

    print(f"WindowServer invalid-window messages (last 30s): {window_error_count()}")
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1
    print(f"PASS: {args.mode}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
