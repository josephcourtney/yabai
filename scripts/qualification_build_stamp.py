#!/usr/bin/env python3
"""Stamp a signed yabai build and verify exact qualification provenance.

The stamp is written by `just build` after signing. Qualification refuses
unknown/stale binaries, changed tracked sources, or modified binary bytes.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "bin" / "yabai"
STAMP = ROOT / "bin" / "qualification-build.json"


def git(*args: str) -> str:
    return subprocess.run(
        ["git", "-C", str(ROOT), *args],
        capture_output=True, text=True, check=True,
    ).stdout.strip()


def fingerprint(binary: Path = BINARY) -> str:
    hasher = hashlib.sha256()
    with binary.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            hasher.update(block)
    return hasher.hexdigest()


def tracked_changes() -> bool:
    return bool(git("status", "--porcelain", "--untracked-files=no"))


def build_record(head: str, sha256: str, dirty: bool) -> dict:
    return {"commit": head, "binary_sha256": sha256, "tracked_dirty": dirty}


def record_issues(record: object, head: str, sha256: str, dirty: bool) -> list[str]:
    if not isinstance(record, dict):
        return ["invalid qualification build record"]
    problems = []
    if record.get("commit") != head:
        problems.append("binary stamp does not match checked-out HEAD")
    if record.get("binary_sha256") != sha256:
        problems.append("signed binary hash does not match build stamp")
    if record.get("tracked_dirty") is not False:
        problems.append("build was stamped with modified tracked files")
    if dirty:
        problems.append("tracked files have changed since checkout")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("write", "verify"))
    args = parser.parse_args()
    try:
        if not BINARY.is_file():
            raise ValueError(f"binary missing: {BINARY}")
        head = git("rev-parse", "HEAD")
        sha256 = fingerprint()
        dirty = tracked_changes()
        if args.mode == "write":
            record = build_record(head, sha256, dirty)
            STAMP.write_text(json.dumps(record, indent=2) + "\n")
            print(f"Build stamp: HEAD={head[:12]} SHA256={sha256[:16]} "
                  f"tracked_dirty={dirty}")
            if dirty:
                print("WARNING: dirty build cannot pass qualification", file=sys.stderr)
            return 0

        if not STAMP.is_file():
            raise ValueError("build stamp missing; run just build after checkout")
        try:
            record = json.loads(STAMP.read_text())
        except (ValueError, OSError) as exc:
            raise ValueError("build stamp is unreadable or invalid") from exc
        issues = record_issues(record, head, sha256, dirty)
        if issues:
            raise ValueError("; ".join(issues) + "; run just build from clean checkout")
        print(f"Build provenance: HEAD={head[:12]} SHA256={sha256[:16]} verified")
        return 0
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
