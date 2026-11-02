"""Pure checks for signed-binary qualification provenance."""
import importlib.util
from pathlib import Path

import pytest


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "qualification_build_stamp.py"
SPEC = importlib.util.spec_from_file_location("qualification_build_stamp", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


@pytest.fixture
def signed_build():
    return module.build_record("abcdef", "012345", False)


def test_clean_matching_record(signed_build):
    assert module.record_issues(signed_build, "abcdef", "012345", False) == []


@pytest.mark.parametrize(("head", "hash_value", "expected"), [
    ("newhead", "012345", "binary stamp does not match checked-out HEAD"),
    ("abcdef", "otherhash", "signed binary hash does not match build stamp"),
])
def test_rejects_revision_or_binary_mismatch(signed_build, head, hash_value, expected):
    assert expected in module.record_issues(signed_build, head, hash_value, False)


def test_reject_dirty_build_and_worktree(signed_build):
    assert module.record_issues(
        module.build_record("abcdef", "012345", True), "abcdef", "012345", False
    )
    assert "tracked files have changed since checkout" in module.record_issues(
        signed_build, "abcdef", "012345", True
    )


@pytest.mark.parametrize("record", [None, {}])
def test_rejects_missing_or_malformed_stamp(record):
    assert module.record_issues(record, "abcdef", "012345", False)


def test_build_recipe_and_preflight_enforce_stamp():
    root = SCRIPT.parents[1]
    justfile = (root / "justfile").read_text()
    build_recipe = justfile.split("\nbuild:\n", 1)[1].split("\n\n", 1)[0]
    assert "qualification_build_stamp.py write" in build_recipe
    assert build_recipe.index("codesign --verify") < build_recipe.index(
        "qualification_build_stamp.py write"
    )
    preflight = (root / "scripts" / "qualify_runtime.py").read_text()
    assert '"qualification_build_stamp.py"' in preflight
    assert '"verify", check=False' in preflight
