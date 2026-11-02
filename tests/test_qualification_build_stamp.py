"""Pure unit checks for signed-binary qualification provenance."""
import importlib.util
from pathlib import Path
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "qualification_build_stamp.py"
SPEC = importlib.util.spec_from_file_location("qualification_build_stamp", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


class ProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.record = module.build_record("abcdef", "012345", False)

    def test_clean_matching_record(self):
        self.assertEqual(module.record_issues(self.record, "abcdef", "012345", False), [])

    def test_reject_checkout_after_build(self):
        self.assertIn(
            "binary stamp does not match checked-out HEAD",
            module.record_issues(self.record, "newhead", "012345", False),
        )

    def test_reject_binary_replacement(self):
        self.assertIn(
            "signed binary hash does not match build stamp",
            module.record_issues(self.record, "abcdef", "otherhash", False),
        )

    def test_reject_dirty_build_and_worktree(self):
        self.assertTrue(module.record_issues(
            module.build_record("abcdef", "012345", True), "abcdef", "012345", False
        ))
        self.assertIn(
            "tracked files have changed since checkout",
            module.record_issues(self.record, "abcdef", "012345", True),
        )

    def test_missing_or_malformed_record(self):
        self.assertTrue(module.record_issues(None, "abcdef", "012345", False))
        self.assertTrue(module.record_issues({}, "abcdef", "012345", False))

    def test_build_recipe_and_preflight_enforce_stamp(self):
        root = SCRIPT.parents[1]
        justfile = (root / "justfile").read_text()
        build_recipe = justfile.split("\nbuild:\n", 1)[1].split("\n\n", 1)[0]
        self.assertIn("qualification_build_stamp.py write", build_recipe)
        self.assertLess(
            build_recipe.index("codesign --verify"),
            build_recipe.index("qualification_build_stamp.py write"),
        )
        preflight = (root / "scripts" / "qualify_runtime.py").read_text()
        self.assertIn('"qualification_build_stamp.py"', preflight)
        self.assertIn('"verify", check=False', preflight)


if __name__ == "__main__":
    unittest.main()
