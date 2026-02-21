#!/usr/bin/env python3
"""
dacom_migrate.py
----------------
Converts legacy DACOM interface map macro blocks into the new
GetInterfaceMap() / getter function style.

Usage:
    python dacom_migrate.py input.h
    python dacom_migrate.py input.h -o output.h
    python dacom_migrate.py input.h --inplace       # overwrites input file
    cat input.h | python dacom_migrate.py -          # stdin

Input example:
    BEGIN_DACOM_MAP_INBOUND(HotkeyEvent)
        DACOM_INTERFACE_ENTRY(IHotkeyEvent)
        DACOM_INTERFACE_ENTRY2(IID_IHotkeyEvent, IHotkeyEvent)
    END_DACOM_MAP()

Output example:
    static IDAComponent* GetIHotkeyEvent(void* self) {
        return static_cast<IHotkeyEvent*>(
            static_cast<HotkeyEvent*>(self));
    }
    static std::span<const DACOMInterfaceEntry2> GetInterfaceMap() {
        static const DACOMInterfaceEntry2 map[] = {
            {"IHotkeyEvent",     &GetIHotkeyEvent},
            {IID_IHotkeyEvent,   &GetIHotkeyEvent},
        };
        return map;
    }
"""

import re
import sys
import argparse
from dataclasses import dataclass, field
from typing import Optional


# ─── data model ───────────────────────────────────────────────────────────────

@dataclass
class Entry:
    """One row in the interface map."""
    key: str            # string key   e.g. "IHotkeyEvent" or IID_IHotkeyEvent
    interface: str      # C++ type     e.g. IHotkeyEvent
    key_is_string: bool # True  -> emit {"key", ...}
                        # False -> emit {key,   ...}  (macro / IID constant)


@dataclass
class MapBlock:
    concrete: str           # e.g. HotkeyEvent
    kind: str               # "inbound" | "outbound"
    entries: list[Entry] = field(default_factory=list)
    raw_start: int = 0      # line index in source
    raw_end: int = 0


# ─── parser ───────────────────────────────────────────────────────────────────

_RE_BEGIN_IN  = re.compile(r'BEGIN_DACOM_MAP_INBOUND\s*\(\s*(\w+)\s*\)')
_RE_BEGIN_OUT = re.compile(r'BEGIN_DACOM_MAP_OUTBOUND\s*\(\s*(\w+)\s*\)')
_RE_END       = re.compile(r'END_DACOM_MAP\s*\(\s*\)')
_RE_ENTRY1    = re.compile(r'DACOM_INTERFACE_ENTRY\s*\(\s*(\w+)\s*\)')
_RE_ENTRY2    = re.compile(r'DACOM_INTERFACE_ENTRY2\s*\(\s*([^,]+?)\s*,\s*(\w+)\s*\)')


def parse_blocks(lines: list[str]) -> list[tuple[MapBlock, list[str]]]:
    """
    Returns a list of (MapBlock, original_lines) tuples, one per macro block
    found in the source.  original_lines is the verbatim text that will be
    replaced.
    """
    blocks = []
    i = 0
    while i < len(lines):
        line = lines[i]

        m_in  = _RE_BEGIN_IN.search(line)
        m_out = _RE_BEGIN_OUT.search(line)
        m = m_in or m_out
        if not m:
            i += 1
            continue

        kind     = "inbound" if m_in else "outbound"
        concrete = m.group(1)
        block    = MapBlock(concrete=concrete, kind=kind, raw_start=i)
        start    = i
        i       += 1

        while i < len(lines):
            l = lines[i]

            if _RE_END.search(l):
                block.raw_end = i
                original = lines[start:i + 1]
                blocks.append((block, original))
                i += 1
                break

            m1 = _RE_ENTRY1.search(l)
            if m1:
                iface = m1.group(1)
                block.entries.append(Entry(key=iface, interface=iface, key_is_string=True))
                i += 1
                continue

            m2 = _RE_ENTRY2.search(l)
            if m2:
                key   = m2.group(1).strip()
                iface = m2.group(2).strip()
                # key is a string literal if it starts with a quote
                is_str = key.startswith('"')
                block.entries.append(Entry(key=key, interface=iface, key_is_string=is_str))
                i += 1
                continue

            i += 1

    return blocks


# ─── code generator ───────────────────────────────────────────────────────────

def _getter_name(iface: str) -> str:
    return f"Get{iface}"


def _unique_interfaces(entries: list[Entry]) -> list[str]:
    """Return de-duplicated list of interface types preserving order."""
    seen = set()
    result = []
    for e in entries:
        if e.interface not in seen:
            seen.add(e.interface)
            result.append(e.interface)
    return result


def generate(block: MapBlock, indent: str = "    ") -> str:
    concrete   = block.concrete
    entries    = block.entries
    interfaces = _unique_interfaces(entries)

    lines = []

    # ── getter functions ──────────────────────────────────────────────────────
    for iface in interfaces:
        getter = _getter_name(iface)
        lines += [
            f"{indent}static IDAComponent* {getter}(void* self) {{",
            f"{indent}    return static_cast<{iface}*>(",
            f"{indent}        static_cast<{concrete}*>(self));",
            f"{indent}}}",
        ]

    lines.append("")

    # ── map function name ─────────────────────────────────────────────────────
    fn = "GetInterfaceMap" if block.kind == "inbound" else "GetInterfaceMapOut"

    # ── column alignment for the map entries ─────────────────────────────────
    # build key strings first so we can align the getter column
    key_strs = []
    for e in entries:
        if e.key_is_string:
            key_strs.append(f'"{e.key}"')
        else:
            key_strs.append(e.key)
    max_key_len = max((len(k) for k in key_strs), default=0)

    # ── GetInterfaceMap() body ────────────────────────────────────────────────
    lines += [
        f"{indent}static std::span<const DACOMInterfaceEntry2> {fn}() {{",
        f"{indent}    static const DACOMInterfaceEntry2 map[] = {{",
    ]

    for key_str, e in zip(key_strs, entries):
        getter  = _getter_name(e.interface)
        padding = " " * (max_key_len - len(key_str))
        lines.append(f"{indent}        {{{key_str},{padding} &{getter}}},")

    lines += [
        f"{indent}    }};",
        f"{indent}    return map;",
        f"{indent}}}",
    ]

    return "\n".join(lines)


# ─── replacer ─────────────────────────────────────────────────────────────────

def migrate_source(source: str) -> tuple[str, int]:
    """
    Replace all macro blocks in source with generated code.
    Returns (new_source, number_of_blocks_replaced).
    """
    lines  = source.splitlines(keepends=True)
    blocks = parse_blocks([l.rstrip('\n').rstrip('\r') for l in lines])

    if not blocks:
        return source, 0

    # Detect dominant indentation from the first block's BEGIN line
    # (used to match surrounding code style)
    result_lines = list(lines)

    # Process in reverse order so line indices stay valid
    for block, original_lines in reversed(blocks):
        start = block.raw_start
        end   = block.raw_end

        # Detect indentation of the BEGIN line
        begin_line = lines[start] if start < len(lines) else ""
        indent_match = re.match(r'^(\s*)', begin_line)
        indent = indent_match.group(1) if indent_match else "\t"

        generated = generate(block, indent=indent)
        replacement = generated + "\n"

        result_lines[start:end + 1] = [replacement]

    return "".join(result_lines), len(blocks)


# ─── CLI ──────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(
        description="Convert DACOM macro interface maps to GetInterfaceMap() style."
    )
    ap.add_argument("input", help="Source file to process, or - for stdin")
    ap.add_argument("-o", "--output", help="Output file (default: stdout)")
    ap.add_argument("--inplace", action="store_true",
                    help="Overwrite input file (ignored when input is -)")
    ap.add_argument("--dry-run", action="store_true",
                    help="Print what would change without writing anything")
    args = ap.parse_args()

    # ── read ──────────────────────────────────────────────────────────────────
    if args.input == "-":
        source = sys.stdin.read()
        input_path = None
    else:
        with open(args.input, "r", encoding="utf-8") as f:
            source = f.read()
        input_path = args.input

    # ── transform ─────────────────────────────────────────────────────────────
    new_source, count = migrate_source(source)

    if count == 0:
        print("No DACOM macro blocks found.", file=sys.stderr)
        sys.exit(0)

    print(f"Migrated {count} block(s).", file=sys.stderr)

    if args.dry_run:
        print(new_source)
        sys.exit(0)

    # ── write ─────────────────────────────────────────────────────────────────
    if args.inplace and input_path:
        with open(input_path, "w", encoding="utf-8") as f:
            f.write(new_source)
        print(f"Written back to {input_path}", file=sys.stderr)
    elif args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(new_source)
        print(f"Written to {args.output}", file=sys.stderr)
    else:
        sys.stdout.write(new_source)


if __name__ == "__main__":
    main()
