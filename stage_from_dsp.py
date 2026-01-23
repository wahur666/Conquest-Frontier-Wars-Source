import sys
import subprocess
import pathlib

EXTS = {".cpp", ".c", ".asm", ".rc", ".h", ".hpp"}

def parse_dsp(dsp_path):
    dsp_path = pathlib.Path(dsp_path).resolve()
    base_dir = dsp_path.parent
    files = []

    with dsp_path.open("r", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line.startswith("SOURCE="):
                continue

            rel = line.split("=", 1)[1].strip().replace("\\", "/")
            p = (base_dir / rel).resolve()
            if p.suffix.lower() in EXTS:
                files.append(p)

    return files

def main():
    if len(sys.argv) != 2:
        print("usage: python stage_from_dsp.py Conquest.dsp")
        sys.exit(1)

    dsp = sys.argv[1]
    files = parse_dsp(dsp)

    to_add = []
    for f in files:
        if f.exists():
            to_add.append(str(f))
        else:
            print(f"warning: missing file: {f}")

    if not to_add:
        print("no files to add")
        return

    subprocess.run(["git", "add", "--"] + to_add, check=True)
    print(f"staged {len(to_add)} files")

if __name__ == "__main__":
    main()
