#!/usr/bin/env python3
"""One-command, fail-closed Mission Control diagnostic session.

The Mission Control drag itself remains manual; all other stages are automated.
Temporarily pauses a recognized Homebrew service and restores it on teardown.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
from pathlib import Path
import signal
import subprocess
import sys
import tarfile
import time

from mission_control_assertions import assess_transfer

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


def pause_installed_service(folder: Path) -> bool:
    result = run("pgrep", "-x", "yabai")
    if result.returncode == 1:
        return False
    if result.returncode:
        raise RuntimeError("cannot inspect existing yabai daemon")
    launch = require("launchctl", "list")
    if "com.koekeishiya.yabai" not in launch:
        raise RuntimeError("existing yabai daemon is not a recognized launchd service")
    installed = Path("/opt/homebrew/bin/yabai")
    if not installed.is_file():
        raise RuntimeError("installed yabai executable unavailable")
    require(str(installed), "--stop-service")
    for _ in range(40):
        if run("pgrep", "-x", "yabai").returncode == 1:
            (folder / "service-state.txt").write_text("Service stopped; restart required.\\n")
            return True
        time.sleep(0.25)
    raise RuntimeError("installed service did not stop")


def resume_installed_service(folder: Path) -> None:
    result = run("/opt/homebrew/bin/yabai", "--start-service")
    (folder / "service-restoration.txt").write_text(
        f"exit={result.returncode}\\n{result.stdout}{result.stderr}"
    )
    if result.returncode:
        print("WARNING: installed service restart failed", file=sys.stderr)


def snapshot(folder: Path, name: str, space_indices: tuple[int, int] | None = None) -> None:
    for kind in ("spaces", "windows", "displays"):
        result = run(str(BINARY), "-m", "query", "--" + kind, timeout=15)
        (folder / f"{name}-{kind}.json").write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f"{name} {kind} query failed: {result.stderr}")
        json.loads(result.stdout)
    if space_indices:
        for index in space_indices:
            result = run(str(BINARY), "-m", "space", str(index), "--dump-layout", timeout=15)
            (folder / f"{name}-bsp-{index}.json").write_text(result.stdout)
            if result.returncode:
                raise RuntimeError(f"{name} BSP dump for Space {index} failed: {result.stderr}")
            json.loads(result.stdout)


def spaces() -> list[dict]:
    data = json.loads(require(str(BINARY), "-m", "query", "--spaces", timeout=15))
    if not isinstance(data, list):
        raise RuntimeError("unexpected Space query response")
    return data


def selected_spaces(source: int, destination: int) -> tuple[dict, dict]:
    entries = {space["index"]: space for space in spaces()}
    if source not in entries or destination not in entries:
        raise RuntimeError("selected Space index is missing")
    selected = entries[source], entries[destination]
    for space in selected:
        if space.get("is-native-fullscreen") or space.get("type") == "fullscreen":
            raise RuntimeError("refusing to modify fullscreen Space")
        if space.get("type") not in ("float", "bsp"):
            raise RuntimeError(f"unsupported original layout: {space.get('type')}")
    return selected


def set_layout(index: int, layout: str) -> None:
    require(str(BINARY), "-m", "space", str(index), "--layout", layout, timeout=15)


def restore_layouts(original: dict[int, tuple[int, str]], folder: Path) -> None:
    errors = []
    for index, (sid, layout) in original.items():
        try:
            current = selected_spaces(index, index)[0]
            if current["id"] != sid:
                raise RuntimeError(f"Space {index} ID changed from {sid} to {current['id']}")
            if current["type"] != layout:
                set_layout(index, layout)
            if selected_spaces(index, index)[0]["type"] != layout:
                raise RuntimeError(f"Space {index} restoration not verified")
        except Exception as exc:
            errors.append(str(exc))
    (folder / "restoration.txt").write_text(
        "RESTORED original layouts\n" if not errors else "RESTORATION FAILED:\n" + "\n".join(errors) + "\n"
    )
    if errors:
        print("WARNING: layout restoration failed; see restoration.txt", file=sys.stderr)


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
    parser.add_argument("--source", type=int, required=True, help="Source Space index")
    parser.add_argument("--destination", type=int, required=True, help="Destination Space index")
    parser.add_argument("--allow-layout-mutation", action="store_true",
                        help="Explicitly permit temporary BSP configuration of both Spaces")
    parser.add_argument("--skip-build", action="store_true", help="Use an already stamped binary")
    parser.add_argument("--trials", type=int, default=1, help="Number of manual transfers per daemon session")
    parser.add_argument("--reverse", action="store_true", help="Alternate transfer direction each trial")
    parser.add_argument("--settle", type=float, default=5.5, help="Seconds after the manual drag")
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("requires macOS")
    if not sys.stdin.isatty():
        parser.error("interactive terminal required for the Mission Control drag")
    if args.source == args.destination or min(args.source, args.destination) < 1:
        parser.error("source and destination must be distinct positive Space indices")
    if not args.allow_layout_mutation:
        parser.error("explicit --allow-layout-mutation is required")
    if not 1 <= args.trials <= 20:
        parser.error("--trials must be between 1 and 20")
    if args.settle < 1.2:
        parser.error("--settle must be at least 1.2 seconds")

    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    folder = (args.out or Path("/tmp") / f"yabai-mc-{stamp}").resolve()
    if folder.exists():
        parser.error(f"output directory already exists: {folder}")
    folder.mkdir(parents=True)
    proc = None
    resume_service = False
    original: dict[int, tuple[int, str]] = {}
    try:
        (folder / "head.txt").write_text(require("git", "rev-parse", "HEAD"))
        if not args.skip_build:
            print("Running tests and building qualification binary...", flush=True)
            for cmd in (("just", "test-local"), ("just", "build")):
                require(*cmd, timeout=900)
        resume_service = pause_installed_service(folder)
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
        source, destination = selected_spaces(args.source, args.destination)
        original = {
            args.source: (source["id"], source["type"]),
            args.destination: (destination["id"], destination["type"]),
        }
        (folder / "original-layouts.json").write_text(json.dumps(original, indent=2))
        print("Temporarily configuring selected Spaces as BSP...", flush=True)
        for index in original:
            set_layout(index, "bsp")
        verified = selected_spaces(args.source, args.destination)
        for space, index in zip(verified, (args.source, args.destination)):
            if space["id"] != original[index][0] or space["type"] != "bsp":
                raise RuntimeError(f"Space {index} BSP setup verification failed")
        print(f"Verified BSP source {args.source} (ID {source['id']}) and "
              f"destination {args.destination} (ID {destination['id']}).")
        trial_results = []
        for trial in range(1, args.trials + 1):
            src_idx, dst_idx = args.source, args.destination
            if args.reverse and trial % 2 == 0:
                src_idx, dst_idx = dst_idx, src_idx
            src, dst = selected_spaces(src_idx, dst_idx)
            if src["id"] != original[src_idx][0] or dst["id"] != original[dst_idx][0]:
                raise RuntimeError("Space identities changed between trials")
            prefix = f"trial-{trial:02d}"
            snapshot(folder, prefix + "-before", (src_idx, dst_idx))
            print(f"\\nTrial {trial}/{args.trials}: Mission Control drag ONE window "
                  f"from Space {src_idx} to Space {dst_idx}.")
            input("Press Enter AFTER the transfer is complete (or Ctrl+C to abort): ")
            time.sleep(args.settle)
            snapshot(folder, prefix + "-after", (src_idx, dst_idx))
            before_spaces = json.loads((folder / f"{prefix}-before-spaces.json").read_text())
            after_spaces = json.loads((folder / f"{prefix}-after-spaces.json").read_text())
            after_windows = json.loads((folder / f"{prefix}-after-windows.json").read_text())
            dumps = {
                sid: json.loads((folder / f"{prefix}-after-bsp-{idx}.json").read_text())
                for idx, sid in ((src_idx, src["id"]), (dst_idx, dst["id"]))
            }
            try:
                result = assess_transfer(
                    before_spaces, after_spaces, after_windows, dumps, src["id"], dst["id"]
                )
            except (ValueError, KeyError, TypeError) as exc:
                (folder / f"{prefix}-result.json").write_text(json.dumps({
                    "result": "FAIL", "reason": str(exc),
                    "source_index": src_idx, "destination_index": dst_idx,
                }, indent=2) + "\\n")
                raise RuntimeError(f"{prefix}: {exc}") from exc
            trial_results.append(result)
            (folder / f"{prefix}-result.json").write_text(json.dumps(result, indent=2) + "\\n")
            print(f"PASS {prefix}: window {result['moved_window_id']} transferred; "
                  "BSP membership, leaf counts and non-overlap verified.", flush=True)
        (folder / "result.txt").write_text(
            f"PASS {len(trial_results)}/{args.trials} BSP transfer trials.\\n"
        )
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
            if proc.poll() is None and original:
                restore_layouts(original, folder)
            stop_daemon(proc)
        if resume_service:
            resume_installed_service(folder)
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
        print("Test daemon stopped; previous service restarted when applicable.", flush=True)


if __name__ == "__main__":
    raise SystemExit(main())
