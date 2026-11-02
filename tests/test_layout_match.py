"""Compile and run the actual C window-key matching implementation."""

import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_layout_match_key_identity(tmp_path: Path) -> None:
    program = ROOT / "tests" / "layout_match_test.c"
    executable = tmp_path / "layout-match-test"
    subprocess.run(
        [
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "src"), str(program), "-o", str(executable),
        ],
        check=True,
    )
    subprocess.run([str(executable)], check=True)
