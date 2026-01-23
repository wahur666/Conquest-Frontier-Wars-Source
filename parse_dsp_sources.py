# parse_dsp_sources.py
import sys
import pathlib

SRC_EXTS = {".cpp", ".c", ".asm", ".rc"}

def parse_dsp(path):
    sources = []
    with open(path, "r", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if line.startswith("SOURCE="):
                p = line.split("=", 1)[1].strip()
                p = p.replace("\\", "/")
                ext = pathlib.Path(p).suffix.lower()
                if ext in SRC_EXTS:
                    sources.append(p)
    return sources

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: python parse_dsp_sources.py Conquest.dsp")
        sys.exit(1)

    sources = parse_dsp(sys.argv[1])
    for s in sources:
        print(s)
