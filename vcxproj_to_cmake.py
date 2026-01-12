import sys
import xml.etree.ElementTree as ET
from pathlib import Path

NS = {"msb": "http://schemas.microsoft.com/developer/msbuild/2003"}

def text(node, default=""):
    return node.text.strip() if node is not None and node.text else default

def parse_defines(defs):
    return [d for d in defs.replace("%(PreprocessorDefinitions)", "").split(";") if d]

def parse_libs(libs):
    return [l for l in libs.replace("%(AdditionalDependencies)", "").split(";") if l.lower().endswith(".lib")]

def main(vcxproj_path, out_path):
    tree = ET.parse(vcxproj_path)
    root = tree.getroot()
    name = Path(vcxproj_path).stem

    sources, headers, resources = [], [], []

    for n in root.findall(".//msb:ClCompile", NS):
        if "Include" in n.attrib:
            sources.append(n.attrib["Include"])

    for n in root.findall(".//msb:ClInclude", NS):
        headers.append(n.attrib["Include"])

    for n in root.findall(".//msb:ResourceCompile", NS):
        if "Include" in n.attrib:
            resources.append(n.attrib["Include"])

    includes = set()
    config_defines = {}
    config_libs = {}
    output_name = None

    for cfg in root.findall(".//msb:ItemDefinitionGroup", NS):
        cond = cfg.attrib.get("Condition", "")
        if not cond:
            continue
        cfg_name = cond.split("=='")[1].split("|")[0]

        cl = cfg.find("msb:ClCompile", NS)
        if cl is not None:
            inc = text(cl.find("msb:AdditionalIncludeDirectories", NS))
            for i in inc.split(";"):
                if i and "%(" not in i:
                    includes.add(i)

            defs = parse_defines(text(cl.find("msb:PreprocessorDefinitions", NS)))
            config_defines[cfg_name] = defs

        link = cfg.find("msb:Link", NS)
        if link is not None:
            libs = parse_libs(text(link.find("msb:AdditionalDependencies", NS)))
            config_libs[cfg_name] = libs

            out = text(link.find("msb:OutputFile", NS))
            if out and output_name is None:
                output_name = Path(out).stem

    with open(out_path, "w", encoding="utf-8") as f:
        w = f.write

        w("cmake_minimum_required(VERSION 3.20)\n")
        w(f"project({name} LANGUAGES CXX)\n\n")

        w("add_library(" + name + " SHARED\n")
        for s in sources:
            w(f"    {s}\n")
        for h in headers:
            w(f"    {h}\n")
        for r in resources:
            w(f"    {r}\n")
        w(")\n\n")

        if output_name and output_name != name:
            w(f"set_target_properties({name} PROPERTIES OUTPUT_NAME {output_name})\n\n")

        w(f"target_include_directories({name} PRIVATE\n")
        w("    .\n")
        for i in sorted(includes):
            w(f"    {i}\n")
        w(")\n\n")

        w(f"target_compile_definitions({name} PRIVATE _WINDOWS WIN32)\n\n")

        w(f"target_compile_definitions({name} PRIVATE\n")
        for cfg, defs in config_defines.items():
            if defs:
                joined = ";".join(defs)
                w(f"    $<$<CONFIG:{cfg}>:{joined}>\n")
        w(")\n\n")

        w(f"target_compile_options({name} PRIVATE\n")
        w("    /W4\n")
        w("    $<$<CONFIG:Debug>:/Zi /Od>\n")
        w("    $<$<CONFIG:OptimizeForSpeed>:/O2>\n")
        w("    $<$<CONFIG:Release>:/O2>\n")
        w(")\n\n")

        w(f"target_link_options({name} PRIVATE /FORCE:MULTIPLE)\n\n")

        all_libs = set()
        for libs in config_libs.values():
            all_libs.update(libs)

        if all_libs:
            w(f"target_link_libraries({name} PRIVATE\n")
            for l in sorted(all_libs):
                w(f"    {l}\n")
            w(")\n")

    print(f"Generated {out_path}")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("usage: vcxproj_to_cmake.py input.vcxproj output/CMakeLists.txt")
        sys.exit(1)

    main(sys.argv[1], sys.argv[2])
