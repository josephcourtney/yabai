#!/usr/bin/env python3
"""Audit floating-Space sublayers; optionally test a BSP/stack -> float cycle.

Read-only by default. The opt-in cycle changes a Space's layout and may
rearrange its windows, so use only a disposable Space with a healthy Dock SA.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]


def command(binary, *args):
    result = subprocess.run(
        [str(binary), *args], capture_output=True, text=True, check=False
    )
    if result.returncode:
        raise RuntimeError(
            f"{binary} {' '.join(args)} failed ({result.returncode}): "
            f"{result.stderr.strip()} {result.stdout.strip()}"
        )
    return result.stdout


def snapshot(binary):
    return (
        json.loads(command(binary, "-m", "query", "--spaces")),
        json.loads(command(binary, "-m", "query", "--windows")),
    )


def float_space_anomalies(spaces, windows):
    """Potential stale auto-layers, not proof of a user-unrequested layer."""
    floating = {s["index"] for s in spaces if s.get("type") == "float"}
    return [
        {"space": w["space"], "id": w["id"], "app": w.get("app"),
         "sub_level": w.get("sub-level")}
        for w in windows
        if w.get("space") in floating
        and w.get("layer") == "normal"
        and w.get("sub-layer") == "below"
        and w.get("sub-level", 0) < 0
    ]


def managed_candidates(windows, space_index):
    """Windows lowered by yabai while in a managed layout."""
    return {
        w["id"]
        for w in windows
        if w.get("space") == space_index
        and not w.get("is-floating", False)
        and w.get("layer") == "normal"
        and w.get("sub-layer") == "below"
        and w.get("sub-level", 0) < 0
    }


def layer_results(windows, ids):
    by_id = {w["id"]: w for w in windows}
    return {
        wid: {
            "present": wid in by_id,
            "sub_level": by_id[wid].get("sub-level") if wid in by_id else None,
            "sub_layer": by_id[wid].get("sub-layer") if wid in by_id else None,
        }
        for wid in sorted(ids)
    }


def qualify_cycle(binary, space_index, pause):
    # Guard against testing an installed or unexpectedly running daemon.
    subprocess.run(
        [sys.executable, str(ROOT / "scripts" / "qualify_runtime.py"), "running"],
        check=True,
    )
    command(binary, "--check-sa")
    spaces, windows = snapshot(binary)
    space = next((s for s in spaces if s["index"] == space_index), None)
    if space is None:
        raise RuntimeError(f"Space index {space_index} not found")
    if not space.get("has-focus"):
        raise RuntimeError("Cycle test requires the selected Space to be active")
    if space.get("type") not in ("bsp", "stack"):
        raise RuntimeError("Cycle test requires an existing BSP or stack Space")

    initial_layout = space["type"]
    ids = managed_candidates(windows, space_index)
    if not ids:
        raise RuntimeError("No lowered, managed windows found; test would be inconclusive")

    float_windows = []
    did_change = False
    try:
        # An IPC failure may occur after the daemon applies the transition.
        # Attempt restoration even if the request itself reports an error.
        did_change = True
        command(binary, "-m", "space", str(space_index), "--layout", "float")
        time.sleep(pause)
        mid_spaces, float_windows = snapshot(binary)
        current = next((s for s in mid_spaces if s["id"] == space["id"]), None)
        if not current or current.get("type") != "float":
            raise RuntimeError("Space did not enter float layout")
    finally:
        if did_change:
            # Restore even if querying or the float-state assertion fails.
            command(binary, "-m", "space", str(space_index), "--layout", initial_layout)
            time.sleep(pause)

    end_spaces, end_windows = snapshot(binary)
    restored = next((s for s in end_spaces if s["id"] == space["id"]), None)
    float_levels = layer_results(float_windows, ids)
    final_levels = layer_results(end_windows, ids)

    # All initially managed auto-layer windows should be normal when floating
    # and lowered again after retiling, provided the SA is working.
    float_ok = all(v["present"] and v["sub_level"] == 0 for v in float_levels.values())
    retile_ok = (
        restored is not None
        and restored.get("type") == initial_layout
        and all(v["present"] and v["sub_level"] < 0 for v in final_levels.values())
    )
    print(json.dumps({
        "space_index": space_index, "space_id": space["id"],
        "initial_layout": initial_layout, "candidate_window_ids": sorted(ids),
        "while_floating": float_levels, "after_restore": final_levels,
        "float_layer_pass": float_ok, "retile_pass": retile_ok,
    }, indent=2))
    return 0 if float_ok and retile_ok else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "bin" / "yabai")
    parser.add_argument("--cycle-index", type=int, metavar="N")
    parser.add_argument("--allow-layout-mutation", action="store_true")
    parser.add_argument("--pause", type=float, default=1.0)
    args = parser.parse_args()
    if args.cycle_index is not None:
        if not args.allow_layout_mutation:
            parser.error("--cycle-index requires --allow-layout-mutation")
        if args.pause < 0.2:
            parser.error("--pause must be at least 0.2 seconds")
        try:
            return qualify_cycle(args.binary, args.cycle_index, args.pause)
        except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
            print(f"FAIL: {exc}", file=sys.stderr)
            return 2

    try:
        spaces, windows = snapshot(args.binary)
        flagged = float_space_anomalies(spaces, windows)
        print(json.dumps({"potential_stale_sublayers": flagged}, indent=2))
        return 0  # An audit finding is not proof of a defect or a test failure.
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
