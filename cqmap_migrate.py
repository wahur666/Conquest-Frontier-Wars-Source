#!/usr/bin/env python3
"""
cqmap_migrate.py
----------------
Converts legacy CQ interface map macro blocks (TObject.h style) into the new
_CQGetEntriesIn() / getter function style used by TObjectX.h.

Usage:
    python cqmap_migrate.py input.h
    python cqmap_migrate.py input.h -o output.h
    python cqmap_migrate.py input.h --inplace       # overwrites input file
    cat input.h | python cqmap_migrate.py -          # stdin

Input example (old TObject.h macros):

    BEGIN_MAP_INBOUND(MyUnit)
        _INTERFACE_ENTRY(IUnit)
        _INTERFACE_ENTRY_(ISelectableID, ISelectable)
        _INTERFACE_ENTRY_AGGREGATE(IHealthID, m_health)
    END_MAP()

Output example (TObjectX.h style):

    static IBaseObject* GetIUnit(void* self) {
        return static_cast<IUnit*>(
            static_cast<MyUnit*>(self));
    }
    static IBaseObject* GetISelectable(void* self) {
        return static_cast<ISelectable*>(
            static_cast<MyUnit*>(self));
    }
    static IBaseObject* GetIHealth(void* self) {
        return static_cast<MyUnit*>(self)->m_health;
    }

    static std::span<const CQInterfaceEntry> _CQGetEntriesIn() {
        static const CQInterfaceEntry map[] = {
            {IUnitID,        &GetIUnit},
            {ISelectableID,  &GetISelectable},
            {IHealthID,      &GetIHealth},
        };
        return map;
    }

Macro reference:

    _INTERFACE_ENTRY(IFoo)
        key  = IFooID   (appends ID suffix automatically)
        type = IFoo
        kind = direct cast

    _INTERFACE_ENTRY_(SomeID, IFoo)
        key  = SomeID   (explicit ID constant, not a string)
        type = IFoo
        kind = direct cast

    _INTERFACE_ENTRY_AGGREGATE(SomeID, m_member)
        key    = SomeID
        member = m_member   (pointer member, dereference in getter)
        kind   = aggregate
"""

import re
import sys
import argparse
from dataclasses import dataclass, field


# ─── data model ───────────────────────────────────────────────────────────────

@dataclass
class Entry:
    key: str            # OBJID constant,  e.g. IUnitID
    interface: str      # C++ type,        e.g. IUnit   (empty for aggregate)
    member: str         # member name for aggregate entries, else empty
    kind: str           # "direct" | "aggregate"


@dataclass
class MapBlock:
    concrete: str
    kind: str               # "inbound" | "outbound"
    entries: list[Entry] = field(default_factory=list)
    raw_start: int = 0
    raw_end: int = 0


# ─── parser ───────────────────────────────────────────────────────────────────

_RE_BEGIN_IN  = re.compile(r'BEGIN_MAP_INBOUND\s*\(\s*(\w+)\s*\)')
_RE_BEGIN_OUT = re.compile(r'BEGIN_MAP_OUTBOUND\s*\(\s*(\w+)\s*\)')
_RE_END       = re.compile(r'END_MAP\s*\(\s*\)')

# _INTERFACE_ENTRY(IFoo)  ->  key=IFooID, type=IFoo
_RE_ENTRY1    = re.compile(r'_INTERFACE_ENTRY\s*\(\s*(\w+)\s*\)')

# _INTERFACE_ENTRY_(SomeID, IFoo)  ->  key=SomeID, type=IFoo
_RE_ENTRY2    = re.compile(r'_INTERFACE_ENTRY_\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)')

# _INTERFACE_ENTRY_AGGREGATE(SomeID, m_member)
_RE_AGGREGATE = re.compile(r'_INTERFACE_ENTRY_AGGREGATE\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)')


def parse_blocks(lines: list[str]) -> list[tuple[MapBlock, list[str]]]:
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
                blocks.append((block, lines[start:i + 1]))
                i += 1
                break

            # aggregate must be checked before entry1/entry2
            # because _INTERFACE_ENTRY_ is a prefix of _INTERFACE_ENTRY_AGGREGATE
            ma = _RE_AGGREGATE.search(l)
            if ma:
                block.entries.append(Entry(
                    key=ma.group(1),
                    interface="",
                    member=ma.group(2),
                    kind="aggregate",
                ))
                i += 1
                continue

            m2 = _RE_ENTRY2.search(l)
            if m2:
                block.entries.append(Entry(
                    key=m2.group(1),
                    interface=m2.group(2),
                    member="",
                    kind="direct",
                ))
                i += 1
                continue

            m1 = _RE_ENTRY1.search(l)
            if m1:
                iface = m1.group(1)
                block.entries.append(Entry(
                    key=f"{iface}ID",   # _INTERFACE_ENTRY(IFoo) -> IFooID
                    interface=iface,
                    member="",
                    kind="direct",
                ))
                i += 1
                continue

            i += 1

    return blocks


# ─── code generator ───────────────────────────────────────────────────────────

def _getter_name(entry: Entry) -> str:
    if entry.kind == "aggregate":
        # derive a readable name from the key, e.g. IHealthID -> GetIHealth
        name = entry.key
        if name.endswith("ID"):
            name = name[:-2]
        return f"Get{name}"
    return f"Get{entry.interface}"


def _unique_entries(entries: list[Entry]) -> list[Entry]:
    """De-duplicate by getter name, preserving order."""
    seen = set()
    result = []
    for e in entries:
        g = _getter_name(e)
        if g not in seen:
            seen.add(g)
            result.append(e)
    return result


def generate(block: MapBlock, indent: str = "    ") -> str:
    concrete = block.concrete
    entries  = block.entries
    unique   = _unique_entries(entries)

    lines = []

    # ── getter functions ──────────────────────────────────────────────────────
    for e in unique:
        getter = _getter_name(e)
        if e.kind == "aggregate":
            # pointer member — dereference directly
            lines += [
                f"{indent}static IBaseObject* {getter}(void* self) {{",
                f"{indent}    return static_cast<{concrete}*>(self)->{e.member};",
                f"{indent}}}",
            ]
        else:
            lines += [
                f"{indent}static IBaseObject* {getter}(void* self) {{",
                f"{indent}    return static_cast<{e.interface}*>(",
                f"{indent}        static_cast<{concrete}*>(self));",
                f"{indent}}}",
            ]

    lines.append("")

    # ── map function name ─────────────────────────────────────────────────────
    fn = "_CQGetEntriesIn" if block.kind == "inbound" else "_CQGetEntriesOut"

    # ── column alignment ──────────────────────────────────────────────────────
    max_key_len = max((len(e.key) for e in entries), default=0)

    # ── map body ──────────────────────────────────────────────────────────────
    lines += [
        f"{indent}static std::span<const CQInterfaceEntry> {fn}() {{",
        f"{indent}    static const CQInterfaceEntry map[] = {{",
    ]

    for e in entries:
        getter  = _getter_name(e)
        padding = " " * (max_key_len - len(e.key))
        lines.append(f"{indent}        {{{e.key},{padding} &{getter}}},")

    lines += [
        f"{indent}    }};",
        f"{indent}    return map;",
        f"{indent}}}",
    ]

    return "\n".join(lines)


# ─── replacer ─────────────────────────────────────────────────────────────────

def migrate_source(source: str) -> tuple[str, int]:
    lines  = source.splitlines(keepends=True)
    flat   = [l.rstrip('\n').rstrip('\r') for l in lines]
    blocks = parse_blocks(flat)

    if not blocks:
        return source, 0

    result_lines = list(lines)

    for block, _ in reversed(blocks):
        start = block.raw_start
        end   = block.raw_end

        begin_line  = lines[start] if start < len(lines) else ""
        indent_m    = __import__('re').match(r'^(\s*)', begin_line)
        indent      = indent_m.group(1) if indent_m else "\t"

        generated   = generate(block, indent=indent)
        result_lines[start:end + 1] = [generated + "\n"]

    return "".join(result_lines), len(blocks)


# ─── CLI ──────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(
        description="Convert TObject macro interface maps to _CQGetEntriesIn() style (TObjectX.h)."
    )
    ap.add_argument("input", help="Source file to process, or - for stdin")
    ap.add_argument("-o", "--output", help="Output file (default: stdout)")
    ap.add_argument("--inplace", action="store_true",
                    help="Overwrite input file (ignored when input is stdin)")
    ap.add_argument("--dry-run", action="store_true",
                    help="Print result without writing")
    args = ap.parse_args()

    if args.input == "-":
        source     = sys.stdin.read()
        input_path = None
    else:
        with open(args.input, "r", encoding="utf-8") as f:
            source = f.read()
        input_path = args.input

    new_source, count = migrate_source(source)

    if count == 0:
        print("No CQ macro blocks found.", file=sys.stderr)
        sys.exit(0)

    print(f"Migrated {count} block(s).", file=sys.stderr)

    if args.dry_run:
        print(new_source)
        sys.exit(0)

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