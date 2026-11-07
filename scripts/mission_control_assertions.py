"""Pure assertions for Mission Control BSP transfer qualifications.

Use authoritative space/window inventories and BSP dumps captured from the
same running test daemon. No macOS APIs or desktop mutations.
"""
from __future__ import annotations


def leaves(node: dict | None) -> int:
    if node is None:
        return 0
    kind = node.get("type")
    if kind == "leaf":
        return 1
    if kind == "branch":
        return leaves(node["left"]) + leaves(node["right"])
    raise ValueError(f"unexpected BSP node type {kind!r}")


def intersect_area(a: dict, b: dict) -> float:
    x = max(0, min(a["x"] + a["w"], b["x"] + b["w"]) - max(a["x"], b["x"]))
    y = max(0, min(a["y"] + a["h"], b["y"] + b["h"]) - max(a["y"], b["y"]))
    return x * y


def assess_transfer(
    before_spaces: list[dict], after_spaces: list[dict],
    after_windows: list[dict], layouts: dict[int, dict],
    source_id: int, destination_id: int,
) -> dict:
    before = {s["id"]: s for s in before_spaces}
    after = {s["id"]: s for s in after_spaces}
    if source_id == destination_id or any(
        sid not in before or sid not in after for sid in (source_id, destination_id)
    ):
        raise ValueError("Space identities missing or duplicated")
    for sid in (source_id, destination_id):
        if before[sid].get("type") != "bsp" or after[sid].get("type") != "bsp":
            raise ValueError(f"Space {sid} is not BSP throughout")
        if layouts[sid].get("layout") != "bsp":
            raise ValueError(f"Space {sid} lacks a BSP dump")

    before_src = set(before[source_id]["windows"])
    after_src = set(after[source_id]["windows"])
    before_dst = set(before[destination_id]["windows"])
    after_dst = set(after[destination_id]["windows"])
    moved = (before_src - after_src) & (after_dst - before_dst)
    if len(moved) != 1:
        raise ValueError(f"expected one source-to-destination window, found {sorted(moved)}")
    if after_src != before_src - moved or after_dst != before_dst | moved:
        raise ValueError("unrelated window membership changed during trial")

    by_id = {w["id"]: w for w in after_windows}
    for sid in (source_id, destination_id):
        space = after[sid]
        index = space["index"]
        managed = [
            by_id[wid] for wid in space["windows"]
            if wid in by_id and not by_id[wid].get("is-floating", False)
            and not by_id[wid].get("is-minimized", False)
            and not by_id[wid].get("is-sticky", False)
        ]
        for window in managed:
            if window["space"] != index:
                raise ValueError(f"window {window['id']} inventory disagrees with Space {sid}")
            frame = window["frame"]
            if frame["w"] <= 0 or frame["h"] <= 0:
                raise ValueError(f"window {window['id']} has invalid frame")
        count = leaves(layouts[sid]["tree"])
        if count != len(managed):
            raise ValueError(f"Space {sid} BSP leaves={count}, managed windows={len(managed)}")
        for i, left in enumerate(managed):
            for right in managed[i + 1:]:
                # Zero gaps and fractional AX-coordinate rounding can produce
                # subpixel differences; tolerate at most one square pixel.
                if intersect_area(left["frame"], right["frame"]) > 1:
                    raise ValueError(f"Space {sid} tiled windows {left['id']} and "
                                     f"{right['id']} overlap")
    return {
        "result": "PASS",
        "moved_window_id": next(iter(moved)),
        "source_space_id": source_id,
        "destination_space_id": destination_id,
        "source_remaining": len(after_src),
        "destination_remaining": len(after_dst),
        "bsp_leaf_counts": {str(sid): leaves(layouts[sid]["tree"])
                            for sid in (source_id, destination_id)},
    }
