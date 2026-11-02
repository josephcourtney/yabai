"""Pure process-detection checks; no macOS desktop required."""
import importlib.util
from pathlib import Path

import pytest


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "qualify_runtime.py"
SPEC = importlib.util.spec_from_file_location("qualify_runtime", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


@pytest.mark.parametrize("command_line", [
    "/Users/example/.local/bin/scroot menubar",
    "/usr/bin/python3 -m scroot menubar",
    "python /tmp/mund/packages/scroot/src/scroot/worker.py",
    "launchagent org.mund.scroot.menubar",
    "scroot",
])
def test_recognizes_live_scroot_processes(command_line):
    assert module.scroot_process(command_line)


@pytest.mark.parametrize("command_line", [
    "/opt/homebrew/bin/yabai --verbose",
    "/usr/bin/python3 scripts/qualify_runtime.py running",
    "/Applications/kitty.app/Contents/MacOS/kitty",
    "/usr/bin/python3 scripts/qualify_runtime.py --ref origin/integration/test",
    "/usr/bin/python3 /tmp/scrooter.py",
])
def test_does_not_match_unrelated_tools(command_line):
    assert not module.scroot_process(command_line)
