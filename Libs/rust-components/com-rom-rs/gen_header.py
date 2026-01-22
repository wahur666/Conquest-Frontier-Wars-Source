import re
import sys

TYPE_MAP = {
    "i32": "int",
    "u32": "unsigned int",
    "u8": "char",  # fix for string literals
    "c_void": "void",
    "IDAConnectionPoint": "IDAConnectionPoint",
}

def normalize(t):
    t = t.strip()
    t = t.replace(" *", "*").replace("* ", "*")
    return t

def map_type(t):
    t = normalize(t)
    # pointer mapping
    if t.startswith("*mut *mut "):
        return map_type(t[10:]) + "**"
    if t.startswith("*mut*mut "):
        return map_type(t[9:]) + "**"
    if t.startswith("*const *mut "):
        return "const " + map_type(t[12:]) + "**"
    if t.startswith("*mut "):
        return map_type(t[5:]) + "*"
    if t.startswith("*const "):
        return "const " + map_type(t[7:]) + "*"
    return TYPE_MAP.get(t, t)

def parse_vtables(src):
    src = re.sub(r"\s+", " ", src)  # collapse whitespace
    structs = {}

    for m in re.finditer(r"pub struct (\w+VTable)\s*{(.*?)}", src):
        name = m.group(1)
        body = m.group(2)

        base = None
        funcs = []

        # detect inheritance marker
        inherit_match = re.search(r'_inherits\s*:\s*&\'static str\s*,?\s*//\s*"(\w+VTable)"', body)
        if inherit_match:
            base = inherit_match.group(1)

        # find all functions
        for fm in re.finditer(
                r"pub (\w+)\s*:\s*extern \"system\" fn\((.*?)\)\s*->\s*([^,}]+)", body
        ):
            fname, args, ret = fm.groups()
            arglist = [a.strip().split(":")[0] for a in args.split(",") if a.strip()]
            funcs.append((fname, arglist, ret.strip()))

        structs[name] = {
            "base": base,
            "funcs": funcs
        }

    return structs

def emit_interface(name, data, structs):
    iface = name.replace("VTable", "")
    out = []

    funcs = []

    # add base functions first
    seen = set()
    if data["base"]:
        for f in structs[data["base"]]["funcs"]:
            if f[0] not in seen:
                funcs.append(f)
                seen.add(f[0])

    for f in data["funcs"]:
        if f[0] not in seen:
            funcs.append(f)
            seen.add(f[0])

    iface_name = name.replace("VTable", "")
    print(f"struct {iface_name};")  # forward declaration

    # vtable struct
    out.append(f"struct {iface}VTable {{")
    for fname, args, ret in funcs:
        retc = map_type(ret)
        carg_types = [map_type(a) for a in args]
        if len(carg_types) > 1:
            out.append(f"    {retc} (__stdcall *{fname})({iface}*, {', '.join(carg_types[1:])});")
        else:
            out.append(f"    {retc} (__stdcall *{fname})({iface}*);")
    out.append("};\n")

    # interface struct
    out.append(f"struct {iface} {{")
    out.append(f"    {iface}VTable* vtable;\n")

    for fname, args, ret in funcs:
        retc = map_type(ret)
        params = []
        call = []
        for i, a in enumerate(args[1:]):
            pname = f"arg{i}"
            params.append(f"{map_type(a)} {pname}")
            call.append(pname)
        out.append(f"    {retc} {fname}({', '.join(params)}) {{")
        out.append(f"        return vtable->{fname}(this{', ' if call else ''}{', '.join(call)});")
        out.append("    }\n")
    out.append("};\n")
    return "\n".join(out)

def main(path):
    src = open(path, "r", encoding="utf-8").read()
    structs = parse_vtables(src)

    print("#pragma once\n")
    print("#ifdef _WIN32")
    print("#define __stdcall __stdcall")
    print("#endif\n")

    for name, data in structs.items():
        print(emit_interface(name, data, structs))

if __name__ == "__main__":
    main(sys.argv[1])
