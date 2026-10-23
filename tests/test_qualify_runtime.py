"""Pure validation tests; do not require a running macOS desktop."""
import importlib.util
from pathlib import Path
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "qualify_runtime.py"
SPEC = importlib.util.spec_from_file_location("qualify_runtime", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


class ScrootDetectionTests(unittest.TestCase):
    def test_live_menu_and_worker(self):
        for cmd in (
            "/Users/example/.local/bin/scroot menubar",
            "/usr/bin/python3 -m scroot menubar",
            "python /tmp/mund/packages/scroot/src/scroot/worker.py",
            "launchagent org.mund.scroot.menubar",
            "scroot",
        ):
            with self.subTest(cmd=cmd):
                self.assertTrue(module.scroot_process(cmd))

    def test_does_not_match_other_tools(self):
        for cmd in (
            "/opt/homebrew/bin/yabai --verbose",
            "/usr/bin/python3 scripts/qualify_runtime.py running",
            "/Applications/kitty.app/Contents/MacOS/kitty",
            "/usr/bin/python3 scripts/qualify_runtime.py --ref origin/integration/test",
            "/usr/bin/python3 /tmp/scrooter.py",
        ):
            with self.subTest(cmd=cmd):
                self.assertFalse(module.scroot_process(cmd))


if __name__ == "__main__":
    unittest.main()
