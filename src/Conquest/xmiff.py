#!/usr/bin/env python3
"""
xmiff.py - Sound Effect Data Compiler
Replacement for the legacy xmiff.exe (16-bit DOS executable)

Usage: python xmiff.py -i <sfxid_header> <input.xmf> <output.dat>
Example: python xmiff.py -i ../DInclude sfxdata.xmf sfxdata.dat
"""

import struct
import re
import sys
import os
from pathlib import Path

class SFXCompiler:
    def __init__(self, sfxid_header_path):
        """Initialize compiler with sfxid.h location"""
        self.enum_map = {}
        self.load_enum_from_header(sfxid_header_path)

    def load_enum_from_header(self, header_dir):
        """Parse sfxid.h and extract enum ID values"""
        header_file = Path(header_dir) / "sfxid.h"

        if not header_file.exists():
            raise FileNotFoundError(f"Cannot find {header_file}")

        with open(header_file, 'r', encoding='utf-8') as f:
            content = f.read()

        # Extract enum body between "enum ID {" and "};"
        match = re.search(r'enum\s+ID\s*\{([^}]+)\}', content, re.DOTALL)
        if not match:
            raise ValueError("Could not find enum ID in header")

        enum_body = match.group(1)
        enum_id = 0

        # Parse enum lines
        for line in enum_body.split('\n'):
            line = line.strip()
            if not line or line.startswith('//'):
                continue

            # Handle explicit assignments: NAME = VALUE,
            if '=' in line:
                match = re.match(r'(\w+)\s*=\s*(\d+)', line)
                if match:
                    name, value = match.groups()
                    enum_id = int(value)
                    self.enum_map[name] = enum_id
                    enum_id += 1
            else:
                # Handle implicit assignment: NAME,
                name = line.rstrip(',').strip()
                if name and name != 'LAST':
                    self.enum_map[name] = enum_id
                    enum_id += 1

        print(f"Loaded {len(self.enum_map)} sound IDs from header")

    def parse_xmf(self, xmf_file):
        """Parse .xmf file and extract SFXCHUNK definitions"""
        with open(xmf_file, 'r', encoding='utf-8') as f:
            content = f.read()

        # Remove C++ comments
        content = re.sub(r'//.*?$', '', content, flags=re.MULTILINE)
        # Remove C comments
        content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)

        entries = []

        # Match SFXCHUNK(...) and SFXCHUNK2(...) calls
        pattern = r'SFXCHUNK2?\s*\(\s*(\w+)\s*,\s*([\d.]+)\s*,\s*"([^"]+)"\s*(?:,\s*([\d.]+))?\s*\)'

        for match in re.finditer(pattern, content):
            sfx_id_name = match.group(1)
            volume = float(match.group(2))
            filename = match.group(3)
            cutoff = float(match.group(4)) if match.group(4) else 0.5

            if sfx_id_name not in self.enum_map:
                print(f"Warning: '{sfx_id_name}' not found in enum, skipping")
                continue

            entries.append({
                'id': self.enum_map[sfx_id_name],
                'id_name': sfx_id_name,
                'volume': volume,
                'filename': filename,
                'cutoff': cutoff
            })

        print(f"Parsed {len(entries)} sound chunks from {xmf_file}")
        return entries

    def write_dat(self, entries, output_file):
        """Write binary .dat file"""
        with open(output_file, 'wb') as f:
            # Write LAST marker value (total enum count, which is the next available ID)
            # This is the highest ID + 1, not the number of entries written
            last_value = max(entry['id'] for entry in entries) + 1
            f.write(struct.pack('<I', last_value))  # uint32_t

            # Write each entry
            for entry in entries:
                # long id (4 bytes, little-endian)
                f.write(struct.pack('<i', entry['id']))

                # float volume (4 bytes, IEEE 754)
                f.write(struct.pack('<f', entry['volume']))

                # char[32] filename (32 bytes, null-padded)
                filename_bytes = entry['filename'].encode('ascii')
                if len(filename_bytes) > 31:
                    print(f"Warning: filename '{entry['filename']}' truncated to 31 chars")
                    filename_bytes = filename_bytes[:31]
                filename_padded = filename_bytes + b'\0' * (32 - len(filename_bytes))
                f.write(filename_padded)

                # float cutoff (4 bytes)
                f.write(struct.pack('<f', entry['cutoff']))

        print(f"Wrote {len(entries)} entries to {output_file}")

def main():
    if len(sys.argv) < 4:
        print("Usage: python xmiff.py -i <sfxid_header_dir> <input.xmf> <output.dat>")
        print("Example: python xmiff.py -i ../DInclude sfxdata.xmf sfxdata.dat")
        sys.exit(1)

    # Parse command line arguments
    if sys.argv[1] != '-i':
        print("Error: Expected '-i' flag for header directory")
        sys.exit(1)

    header_dir = sys.argv[2]
    xmf_file = sys.argv[3]
    output_file = sys.argv[4]

    # Validate inputs
    if not os.path.isdir(header_dir):
        print(f"Error: Header directory '{header_dir}' not found")
        sys.exit(1)

    if not os.path.isfile(xmf_file):
        print(f"Error: XMF file '{xmf_file}' not found")
        sys.exit(1)

    try:
        compiler = SFXCompiler(header_dir)
        entries = compiler.parse_xmf(xmf_file)
        compiler.write_dat(entries, output_file)
        print(f"✓ Successfully compiled {xmf_file} -> {output_file}")
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == '__main__':
    main()