#!/usr/bin/env python3
"""One-command, fail-closed Mission Control diagnostic session.

The Mission Control drag itself remains manual; all other stages are automated.
Never stops an existing yabai daemon or changes the user's launchd services.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tarfile
import time

ROOT = Path(__file__).resolve().parent.parent
BINARY = ROOT / "bin" / "yabai"
CONFIG = ROOT / "scripts" / "qualification.yabairc"


def run(*args: str, timeout: int = 90) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=ROOT, text=True, capture_output=True, timeout=timeout)


def require(*args: str, timeout: int = 90) -> str:
    result = run(*args, timeout=timeout)
    if result.returncode:
        raise RuntimeError(f"{' '.join(args)} failed ({result.returncode}): {result.stderr or result.stdout}")
    return result.stdout


def snapshot(folder: Path, name: str) -> None:
    for kind in ("spaces", "windows", "displays"):
        result = run(str(BINARY), "-m", "query", "--" + kind, timeout=15)
        (folder / f"{name}-{kind}.json").write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f"{name} {kind} query failed: {result.stderr}")
        json.loads(result.stdout)


def stop_daemon(proc: subprocess.Popen[bytes]) -> None:
    if proc.poll() is not None:
        return
    proc.send_signal(signal.SIGINT)
    try:
        proc.wait(timeout=8)
    except subprocess.TimeoutExpired:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, help="Output directory; default is timestamped /tmp directory")
    parser.add_argument("--skip-build", action="store_true", help="Use an already stamped binary")
    parser.add_argument("--settle", type=float, default=2.5, help="Seconds after the manual drag")
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("requires macOS")
    if not sys.stdin.isatty():
        parser.error("interactive terminal required for the Mission Control drag")
    if args.settle < 1.2:
        parser.error("--settle must be at least 1.2 seconds")

    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    folder = (args.out or Path("/tmp") / f"yabai-mc-{stamp}").resolve()
    if folder.exists():
        parser.error(f"output directory already exists: {folder}")
    folder.mkdir(parents=True)
    proc = None
    try:
        (folder / "head.txt").write_text(require("git", "rev-parse", "HEAD"))
        if not args.skip_build:
            print("Running tests and building qualification binary...", flush=True)
            for cmd in (("just", "test-local"), ("just", "build")):
                require(*cmd, timeout=900)
        print("Checking that no yabai daemon is already running...", flush=True)
        (folder / "preflight.log").write_text(
            require("python3", "scripts/qualify_runtime.py", "preflight", timeout=90)
        )
        log = (folder / "daemon.log").open("wb")
        try:
            proc = subprocess.Popen(
                [str(BINARY), "--verbose", "--config", str(CONFIG)],
                cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, start_new_session=True
            )
        finally:
            log.close()
        time.sleep(1.5)
        if proc.poll() is not None:
            raise RuntimeError(f"test daemon exited early: {proc.returncode}")
        (folder / "running.log").write_text(
            require("python3", "scripts/qualify_runtime.py", "running", timeout=90)
        )
        snapshot(folder, "before")
        print("\nTest daemon running. Move ONE window between BSP Spaces using Mission Control.")
        input("Press Enter AFTER the transfer is complete (or Ctrl+C to abort): ")
        time.sleep(args.settle)
        snapshot(folder, "after")
        (folder / "result.txt").write_text("Manual transfer reported complete; snapshots captured.\n")
        print("Captured post-transfer state.", flush=True)
        return 0
    except (KeyboardInterrupt, EOFError):
        (folder / "result.txt").write_text("ABORTED: transfer not confirmed.\n")
        return 130
    except (RuntimeError, subprocess.TimeoutExpired, OSError, ValueError) as exc:
        (folder / "result.txt").write_text(f"FAILED: {exc}\n")
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    finally:
        if proc is not None:
            stop_daemon(proc)
        if (folder / "daemon.log").exists():
            lines = (folder / "daemon.log").read_text(errors="replace").splitlines()
            (folder / "events.log").write_text(
                "\n".join(line for line in lines if any(
                    token in line for token in ("MC_TRACE", "MISSION_CONTROL", "SPACE_CHANGED", "WINDOW_MOVED")
                )) + "\n"
            )
        archive = folder.with_suffix(".tar.gz")
        with tarfile.open(archive, "w:gz") as tar:
            tar.add(folder, arcname=folder.name)
        print(f"Evidence: {archive}", flush=True)
        print("Test daemon stopped; no installed service was modified.", flush=True)


if __name__ == "__main__":
    raise SystemExit(main())
