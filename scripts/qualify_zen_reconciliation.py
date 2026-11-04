#!/usr/bin/env python3
"""Read-only, staged Zen window-discovery and BSP qualification on macOS.

The operator creates/closes windows and switches Spaces manually. The script
never creates windows, changes the active Space, or changes yabai settings.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SPACE_LABEL = "yabai-zen-qualification"
PHASES = ("baseline", "ordinary", "detach1", "detach2", "returned", "closed")


class QualificationError(RuntimeError):
    """A precondition or an observed window-state invariant failed."""


def command(binary: Path, *args: str) -> str:
    try:
        result = subprocess.run(
            [str(binary), "-m", *args], capture_output=True, text=True,
            check=True, timeout=15,
        )
    except subprocess.CalledProcessError as exc:
        detail = (exc.stderr or exc.stdout or "").strip() or f"exit {exc.returncode}"
        raise QualificationError(
            f"yabai {' '.join(args)} failed: {detail}"
        ) from exc
    return result.stdout.strip()


def require_test_space(spaces: list[dict], space_index: int, space_id: int) -> dict:
    """Fail closed before querying a BSP tree or recording phase state."""
    matches = [s for s in spaces if s.get("index") == space_index]
    if len(matches) != 1:
        raise QualificationError("Space index missing or ambiguous")
    space = matches[0]
    if space.get("id") != space_id:
        raise QualificationError("Space ID changed; refusing to retarget")
    if space.get("label") != SPACE_LABEL:
        raise QualificationError("requires a Space labeled yabai-zen-qualification")
    if space.get("type") != "bsp":
        raise QualificationError(
            f"qualification Space must be active and BSP: Space {space_index} "
            f"(ID {space_id}) has layout {space.get('type')!r}; "
            "select a disposable BSP Space; do not convert a work Space"
        )
    if space.get("has-focus") is not True:
        raise QualificationError("qualification Space must be active and BSP (not focused)")
    return space


def read_snapshot(binary: Path, space_index: int, space_id: int) -> dict:
    """Validate Space identity/layout before reading its BSP tree."""
    spaces = json.loads(command(binary, "query", "--spaces"))
    require_test_space(spaces, space_index, space_id)
    windows = json.loads(command(binary, "query", "--windows"))
    if not any(w.get("app") == "Zen" and w.get("space") == space_index
               for w in windows):
        raise QualificationError(
            f"no registered Zen window on Space {space_index}; "
            "open a Zen window in the disposable BSP Space before baseline"
        )
    return {
        "spaces": spaces,
        "windows": windows,
        "layout": json.loads(command(binary, "space", str(space_index), "--dump-layout")),
        "reconciliation": command(binary, "config", "window_reconciliation"),
        "interval": int(command(binary, "config", "window_reconciliation_interval")),
    }


def leaf_apps(node: dict) -> list[str]:
    if node.get("type") == "leaf":
        return node.get("apps", [])
    if node.get("type") != "branch":
        raise QualificationError("invalid BSP dump node type")
    return leaf_apps(node["left"]) + leaf_apps(node["right"])


def observed_zen_ids(snapshot: dict, space_index: int, space_id: int) -> set[int]:
    if snapshot["reconciliation"] != "on":
        raise QualificationError("window_reconciliation is not enabled")
    if not 250 <= snapshot["interval"] <= 10000:
        raise QualificationError("invalid reconciliation interval")

    space = require_test_space(snapshot["spaces"], space_index, space_id)

    all_ids = [w["id"] for w in snapshot["windows"]]
    if len(all_ids) != len(set(all_ids)):
        raise QualificationError("duplicate window IDs in yabai inventory")

    registered = [w for w in snapshot["windows"]
                  if w.get("app") == "Zen" and w.get("space") == space_index]
    ids = {w["id"] for w in registered}
    space_ids = set(space.get("windows", []))
    for w in registered:
        if w["id"] not in space_ids:
            raise QualificationError(f"Zen {w['id']} absent from Space membership")
        if w.get("has-ax-reference") is not True:
            raise QualificationError(f"Zen {w['id']} missing AX reference")
        if w.get("is-floating") is not False:
            raise QualificationError(f"Zen {w['id']} is floating")

    layout = snapshot["layout"]
    if layout.get("layout") != "bsp":
        raise QualificationError("BSP dump has non-BSP layout")
    tree_zen_count = leaf_apps(layout["tree"]).count("Zen")
    if tree_zen_count != len(ids):
        raise QualificationError(
            f"BSP tree contains {tree_zen_count} Zen leaves, inventory contains {len(ids)}"
        )
    return ids


def advance(phase: str, state: dict | None, snapshot: dict,
            binary: Path, space_index: int, space_id: int) -> dict:
    """Pure phase transition; returns an updated state only on full success."""
    ids = observed_zen_ids(snapshot, space_index, space_id)
    binary_path = str(binary.resolve())
    if phase == "baseline":
        if not ids:
            raise QualificationError("open at least one ordinary Zen window first")
        return {
            "phase": phase, "space_index": space_index, "space_id": space_id,
            "binary": binary_path, "baseline": sorted(ids),
            "ordinary": None, "detach1": None, "detach2": None,
        }
    if state is None:
        raise QualificationError("missing baseline state")
    previous = PHASES[PHASES.index(phase) - 1]
    if state.get("phase") != previous:
        raise QualificationError(f"expected previous phase {previous}")
    if (state.get("space_index"), state.get("space_id"), state.get("binary")) != (
        space_index, space_id, binary_path
    ):
        raise QualificationError("binary or Space identity changed")

    expected = set(state["baseline"])
    for name in ("ordinary", "detach1", "detach2"):
        if state[name] is not None:
            expected.add(state[name])

    if phase in ("ordinary", "detach1", "detach2"):
        added = ids - expected
        if len(added) != 1 or not expected <= ids:
            raise QualificationError(
                f"{phase}: expected exactly one newly registered Zen window; "
                f"new={sorted(added)}, missing={sorted(expected - ids)}"
            )
        return {**state, "phase": phase, phase: added.pop()}
    if phase == "returned":
        if ids != expected:
            raise QualificationError("windows changed during Space switch/return")
    elif phase == "closed":
        expected -= {state["detach1"], state["detach2"]}
        if ids != expected:
            raise QualificationError(
                f"detached windows not fully removed, or baseline changed: "
                f"expected={sorted(expected)}, observed={sorted(ids)}"
            )
    return {**state, "phase": phase}


def store_state(path: Path, state: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode="w", dir=path.parent, prefix=".zen-",
                                     suffix=".json", delete=False) as file:
        temp_path = Path(file.name)
        os.chmod(temp_path, 0o600)
        json.dump(state, file, indent=2, sort_keys=True)
        file.write("\n")
    os.replace(temp_path, path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=PHASES)
    parser.add_argument("--binary", type=Path, default=ROOT / "bin" / "yabai")
    parser.add_argument("--space-index", type=int, required=True)
    parser.add_argument("--space-id", type=int, required=True)
    parser.add_argument("--state", type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("live window qualification requires macOS")
    if args.space_index < 1 or args.space_id < 1:
        parser.error("Space index and persistent ID must be positive")
    try:
        if args.phase == "baseline":
            if args.state.exists():
                raise QualificationError("state file already exists; use a fresh path")
            state = None
        else:
            if not args.state.is_file():
                raise QualificationError(
                    f"baseline state missing at {args.state}; "
                    "run baseline successfully on a disposable BSP Space "
                    "before attempting later phases"
                )
            state = json.loads(args.state.read_text())
        snapshot = read_snapshot(args.binary, args.space_index, args.space_id)
        next_state = advance(args.phase, state, snapshot, args.binary,
                             args.space_index, args.space_id)
        store_state(args.state, next_state)
        print(json.dumps({"result": "PASS", **next_state}, sort_keys=True))
        return 0
    except (QualificationError, OSError, subprocess.SubprocessError,
            ValueError, KeyError, TypeError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
