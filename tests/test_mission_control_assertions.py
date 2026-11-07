"""Implementation-independent transfer qualification tests."""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from mission_control_assertions import assess_transfer, leaves


def fixture():
    before = [
        {"id": 405, "index": 1, "type": "bsp", "windows": [10, 11]},
        {"id": 641, "index": 4, "type": "bsp", "windows": [20]},
    ]
    after = [
        {"id": 405, "index": 1, "type": "bsp", "windows": [11]},
        {"id": 641, "index": 4, "type": "bsp", "windows": [20, 10]},
    ]
    windows = [
        {"id": 11, "space": 1, "is-floating": False,
         "frame": {"x": 0, "y": 0, "w": 200, "h": 200}},
        {"id": 20, "space": 4, "is-floating": False,
         "frame": {"x": 0, "y": 0, "w": 100, "h": 200}},
        {"id": 10, "space": 4, "is-floating": False,
         "frame": {"x": 100, "y": 0, "w": 100, "h": 200}},
    ]
    layouts = {
        405: {"layout": "bsp", "tree": {"type": "leaf", "apps": ["kitty"]}},
        641: {"layout": "bsp", "tree": {
            "type": "branch", "left": {"type": "leaf"}, "right": {"type": "leaf"}
        }},
    }
    return before, after, windows, layouts


def test_valid_forward_and_reverse():
    before, after, windows, layouts = fixture()
    assert assess_transfer(before, after, windows, layouts, 405, 641)["moved_window_id"] == 10
    reverse_before = after
    reverse_after = before
    reverse_windows = [
        {**w, "space": 1, "frame": {"x": 0, "y": 0, "w": 100, "h": 200}}
        if w["id"] == 10 else w for w in windows
    ]
    reverse_windows[0]["frame"] = {"x": 100, "y": 0, "w": 100, "h": 200}
    reverse_layouts = {
        405: {"layout": "bsp", "tree": {
            "type": "branch", "left": {"type": "leaf"}, "right": {"type": "leaf"}
        }},
        641: {"layout": "bsp", "tree": {"type": "leaf"}},
    }
    assert assess_transfer(reverse_before, reverse_after, reverse_windows,
                           reverse_layouts, 641, 405)["moved_window_id"] == 10


@pytest.mark.parametrize("mutation", ["no_transfer", "stale_tree", "overlap", "wrong_layout",
                                      "unrelated_change", "wrong_inventory"])
def test_rejects_invalid_qualification(mutation):
    before, after, windows, layouts = fixture()
    if mutation == "no_transfer":
        after[0]["windows"] = [10, 11]
        after[1]["windows"] = [20]
    elif mutation == "stale_tree":
        layouts[641]["tree"] = {"type": "leaf"}
    elif mutation == "overlap":
        windows[-1]["frame"]["x"] = 50
    elif mutation == "wrong_layout":
        after[0]["type"] = "float"
    elif mutation == "unrelated_change":
        after[1]["windows"].append(99)
    elif mutation == "wrong_inventory":
        windows[-1]["space"] = 1
    with pytest.raises(ValueError):
        assess_transfer(before, after, windows, layouts, 405, 641)


def test_empty_and_invalid_tree():
    assert leaves(None) == 0
    assert leaves({"type": "leaf"}) == 1
    with pytest.raises(ValueError):
        leaves({"type": "unknown"})
