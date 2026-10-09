#!/usr/bin/env python3
"""Checks docs/manifest.json: part names and offsets per chip, and version == FW_VERSION.

A wrong offset is silent: esptool happily writes the app over the partition table.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXPECTED = [("bootloader", 0x0), ("partitions", 0x8000), ("boot_app0", 0xE000), ("ledmatrix", 0x10000)]
CHIPS = {"ESP32-C6": "c6", "ESP32-S3": "s3"}


def main() -> int:
    errors = []
    manifest = json.loads((ROOT / "docs/manifest.json").read_text())
    config = (ROOT / "firmware/ledmatrix/config.h").read_text()
    m = re.search(r'#define FW_VERSION "([^"]+)"', config)
    if not m:
        errors.append("FW_VERSION not found in config.h")
    elif manifest.get("version") != m.group(1):
        errors.append(f"manifest version {manifest.get('version')} != FW_VERSION {m.group(1)}")

    seen = set()
    for build in manifest.get("builds", []):
        chip = build.get("chipFamily")
        board = CHIPS.get(chip)
        if not board:
            errors.append(f"unexpected chipFamily {chip}")
            continue
        seen.add(chip)
        parts = [(p.get("path"), p.get("offset")) for p in build.get("parts", [])]
        want = [(f"{name}-{board}.bin", off) for name, off in EXPECTED]
        if parts != want:
            errors.append(f"{chip}: parts {parts} != {want}")
    for chip in CHIPS:
        if chip not in seen:
            errors.append(f"missing build for {chip}")

    if errors:
        for e in errors:
            print(f"manifest: {e}", file=sys.stderr)
        return 1
    print(f"manifest ok (v{manifest['version']})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
