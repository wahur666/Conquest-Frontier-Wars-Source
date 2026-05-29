#!/usr/bin/env python3
"""
Audit Conquest particle flag values in dumped .pte.xml files.

The script only inspects the two particle formats supported by
particle_xml_unify.py:

* modern root-level ParticleSystemParameters
* legacy Particle Event/particle1.Def EventDef
"""

from __future__ import annotations

import argparse
import base64
import struct
import sys
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path
from typing import Iterable


PSP_F_RELATIVE_TRANSFORM = 1 << 0
PSP_F_RELATIVE_VELOCITY = 1 << 1
PSP_F_RENDER_PARTICLE_LIFE = 1 << 3
PSP_F_RENDER_DITHER = 1 << 4
PSP_F_RENDER_FOG = 1 << 5


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path, help="Input .pte.xml files or directories.")
    parser.add_argument("--recursive", action="store_true", help="Recurse into input directories.")
    parser.add_argument("--show-zero", action="store_true", help="Print files whose computed flags are zero too.")
    args = parser.parse_args()

    rows = []
    skipped = 0
    failed = 0

    for path in iter_input_files(args.inputs, args.recursive):
        try:
            result = read_particle_flags(path)
        except Exception as exc:  # noqa: BLE001 - audit should continue over dump folders.
            failed += 1
            print(f"FAIL {path}: {exc}", file=sys.stderr)
            continue
        if result is None:
            skipped += 1
            continue
        rows.append((path, *result))

    counts = Counter(flags for _, _, flags in rows)
    print(f"Audited {len(rows)} particle files, skipped {skipped}, failed {failed}.")
    for flags, count in sorted(counts.items()):
        print(f"flags=0x{flags:08x}: {count}")

    nonzero = [(path, source_format, flags) for path, source_format, flags in rows if flags != 0]
    if nonzero:
        print("\nNon-zero particle flags:")
        for path, source_format, flags in nonzero:
            print(f"0x{flags:08x}  {source_format:31s}  {path}")
    elif rows:
        print("\nAll audited particle flags are zero.")

    if args.show_zero:
        print("\nZero particle flags:")
        for path, source_format, flags in rows:
            if flags == 0:
                print(f"0x{flags:08x}  {source_format:31s}  {path}")

    return 1 if failed else 0


def iter_input_files(inputs: Iterable[Path], recursive: bool) -> Iterable[Path]:
    for input_path in inputs:
        if input_path.is_dir():
            pattern = "**/*.pte.xml" if recursive else "*.pte.xml"
            yield from sorted(input_path.glob(pattern))
        else:
            yield input_path


def read_particle_flags(path: Path) -> tuple[str, int] | None:
    root = ET.parse(path).getroot()

    modern = find_child(root, "file", "ParticleSystemParameters")
    if modern is not None and modern.text:
        data = decode_file(modern)
        if len(data) < 4:
            return None
        return "ParticleSystemParameters", u32(data, 0)

    event_dir = find_child(root, "dir", "Particle Event")
    event_file = find_child(event_dir, "file", "particle1.Def") if event_dir is not None else None
    if event_file is not None and event_file.text:
        data = decode_file(event_file)
        if len(data) < 176:
            return None
        return "Particle Event/EventDef", legacy_event_flags(data)

    return None


def legacy_event_flags(data: bytes) -> int:
    flags = 0

    if data[148]:
        flags |= PSP_F_RENDER_DITHER

    legacy_bitfields = u32(data, 168)
    if legacy_bitfields & 1:
        flags |= PSP_F_RELATIVE_VELOCITY
    if legacy_bitfields & 2:
        flags |= PSP_F_RELATIVE_TRANSFORM

    if len(data) >= 204:
        if i32(data, 196):
            flags |= PSP_F_RENDER_PARTICLE_LIFE
        if i32(data, 200):
            flags |= PSP_F_RENDER_FOG

    return flags


def find_child(parent: ET.Element | None, tag: str, name: str) -> ET.Element | None:
    if parent is None:
        return None
    for child in list(parent):
        if child.tag == tag and child.get("name") == name:
            return child
    return None


def decode_file(element: ET.Element) -> bytes:
    return base64.b64decode("".join((element.text or "").split()))


def i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


if __name__ == "__main__":
    raise SystemExit(main())
