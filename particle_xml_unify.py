#!/usr/bin/env python3
"""
Convert dumped Conquest particle UTF XML into a normalized XML format.

This is non-destructive: input files are never modified. The converter supports
the two particle layouts currently understood by web/particleApp.js:

* modern ParticleSystemParameters files
* legacy Particle Event/particle1.Def EventDef files

Example:
    python particle_xml_unify.py xml_dump --out-dir xml_unified --recursive
"""

from __future__ import annotations

import argparse
import base64
import copy
import math
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Iterable


PSP_NUM_COLOR_KEYS = 32
PSP_TEXTURE_NAME_LEN = 16

PSP_F_RELATIVE_TRANSFORM = 1 << 0
PSP_F_RELATIVE_VELOCITY = 1 << 1
PSP_F_IGNORE_ORIENTATION = 1 << 2
PSP_F_RENDER_PARTICLE_LIFE = 1 << 3
PSP_F_RENDER_DITHER = 1 << 4
PSP_F_RENDER_FOG = 1 << 5

D3DBLEND_ONE = 2


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path, help="Input .xml files or directories.")
    parser.add_argument("--out-dir", type=Path, default=Path("xml_unified"), help="Output directory.")
    parser.add_argument("--recursive", action="store_true", help="Recurse into input directories.")
    parser.add_argument("--overwrite", action="store_true", help="Overwrite existing converted files.")
    parser.add_argument(
        "--no-assets",
        action="store_true",
        help="Do not embed the decoded particle texture image into the unified XML.",
    )
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)

    converted = 0
    skipped = 0
    failed = 0

    for path in iter_input_files(args.inputs, args.recursive):
        try:
            result = convert_file(path, args.out_dir, include_assets=not args.no_assets, overwrite=args.overwrite)
        except Exception as exc:  # noqa: BLE001 - command-line tool should continue over a dump folder.
            failed += 1
            print(f"FAIL {path}: {exc}", file=sys.stderr)
            continue

        if result is None:
            skipped += 1
            print(f"SKIP {path}: no root-level particle data")
        else:
            converted += 1
            print(f"OK   {path} -> {result}")

    print(f"Converted {converted}, skipped {skipped}, failed {failed}.")
    return 1 if failed else 0


def iter_input_files(inputs: Iterable[Path], recursive: bool) -> Iterable[Path]:
    for input_path in inputs:
        if input_path.is_dir():
            pattern = "**/*.xml" if recursive else "*.xml"
            yield from sorted(input_path.glob(pattern))
        else:
            yield input_path


def convert_file(path: Path, out_dir: Path, include_assets: bool, overwrite: bool) -> Path | None:
    tree = ET.parse(path)
    source_root = tree.getroot()
    parsed = parse_particle_root(source_root)
    if parsed is None:
        return None

    parameters, source_format, texture_library, source_notes = parsed
    output_root = build_unified_xml(path, source_root, parameters, source_format, source_notes)

    if include_assets and texture_library is not None:
        texture = decode_texture_library(texture_library, parameters["textureName"])
        if texture is None:
            raise ValueError("could not decode embedded particle texture")
        add_embedded_image(output_root, texture)

    indent(output_root)
    output_path = out_dir / f"{path.stem}.unified.xml"
    if output_path.exists() and not overwrite:
        raise FileExistsError(f"{output_path} exists; pass --overwrite to replace it")

    ET.ElementTree(output_root).write(output_path, encoding="utf-8", xml_declaration=True)
    return output_path


def parse_particle_root(root: ET.Element) -> tuple[dict, str, ET.Element | None, dict] | None:
    modern = find_child(root, "file", "ParticleSystemParameters")
    if modern is not None and modern.text:
        data = decode_file(modern)
        if len(data) < 636:
            return None
        parameters = parse_particle_system_parameters(data)
        return parameters, "ParticleSystemParameters", find_child(root, "dir", "Texture library"), {}

    event_dir = find_child(root, "dir", "Particle Event")
    event_file = find_child(event_dir, "file", "particle1.Def") if event_dir is not None else None
    if event_file is not None and event_file.text:
        data = decode_file(event_file)
        if len(data) < 176:
            return None
        parameters = parse_legacy_event_def(data)
        source_notes = read_legacy_notes(event_dir)
        return parameters, "Particle Event/EventDef", find_child(event_dir, "dir", "Texture library"), source_notes

    return None


def build_unified_xml(
    source_path: Path,
    source_root: ET.Element,
    p: dict,
    source_format: str,
    source_notes: dict,
) -> ET.Element:
    root = ET.Element(
        "particleEditor",
        {
            "format": "cfw-unified-particle",
            "version": "1",
            "sourceFile": str(source_path),
            "sourceRoot": source_root.get("name", ""),
            "sourceFormat": source_format,
        },
    )

    if source_notes:
        notes = ET.SubElement(root, "sourceValues")
        for key, value in source_notes.items():
            ET.SubElement(notes, "value", {"name": key, "value": fmt(value)})

    params = ET.SubElement(root, "parameters")
    ET.SubElement(
        params,
        "rendering",
        {
            "textureName": p["textureName"],
            "textureFps": fmt(p["textureFps"]),
            "srcBlend": str(p["srcBlend"]),
            "dstBlend": str(p["dstBlend"]),
            "boundingSphereRadius": fmt(p["boundingSphereRadius"]),
        },
    )

    emitter = ET.SubElement(
        params,
        "emitter",
        {
            "initialParticleCount": fmt(p["initialParticleCount"]),
            "maxParticleCount": fmt(p["maxParticleCount"]),
            "lifetime": fmt(p["lifetime"]),
            "frequency": fmt(p["frequency"]),
            "nozzleSize": fmt(p["emitterNozzleSize"]),
        },
    )
    add_vector(emitter, "direction", p["emitterDirection"])
    add_vector(emitter, "nozzleDamp", p["emitterNozzleDamp"])

    particles = ET.SubElement(
        params,
        "particles",
        {
            "lifetime": fmt(p["particleLifetime"]),
            "positionRandomizer": fmt(p["particlePositionRandomizer"]),
            "velocity": fmt(p["particleVelocity"]),
            "velocityRandomizer": fmt(p["particleVelocityRandomizer"]),
            "twistVelocity": fmt(p["particleTwistVelocity"]),
            "size": fmt(p["particleSize"]),
            "sizeVelocity": fmt(p["particleSizeVelocity"]),
        },
    )
    add_vector(particles, "gravity", p["gravity"])

    frames = ET.SubElement(params, "colorFrames", {"keyFrameBits": f"0x{p['colorKeyFrameBits']:08x}"})
    for index, frame in enumerate(p["colorFrames"]):
        ET.SubElement(
            frames,
            "frame",
            {
                "index": str(index),
                "key": bool_text((p["colorKeyFrameBits"] & (1 << index)) != 0),
                "r": fmt(frame["r"]),
                "g": fmt(frame["g"]),
                "b": fmt(frame["b"]),
                "a": fmt(frame["a"]),
            },
        )

    return root


def add_vector(parent: ET.Element, name: str, vector: dict) -> None:
    ET.SubElement(parent, name, {"x": fmt(vector["x"]), "y": fmt(vector["y"]), "z": fmt(vector["z"])})


def add_embedded_image(parent: ET.Element, texture: dict) -> None:
    assets = ET.SubElement(parent, "embeddedImages")
    image = ET.SubElement(
        assets,
        "image",
        {
            "name": texture["name"],
            "format": "bmp",
            "encoding": "base64",
            "width": str(texture["width"]),
            "height": str(texture["height"]),
            "channels": "rgba",
            "alpha": texture["alpha"],
        },
    )
    data = ET.SubElement(image, "file", {"name": "Image BMP"})
    data.text = base64.b64encode(encode_bmp32(texture["width"], texture["height"], texture["rgba"])).decode("ascii")


def decode_texture_library(texture_library: ET.Element, texture_name: str) -> dict | None:
    textures = [child for child in list(texture_library) if child.tag == "dir"]
    if not textures:
        return None

    chosen = choose_texture(textures, texture_name)
    width = bytes_i32(find_texture_file(chosen, "Image X size") or b"")
    height = bytes_i32(find_texture_file(chosen, "Image Y size") or b"")
    if width <= 0 or height <= 0:
        return None

    format_node = find_texture_format_node(chosen)
    if format_node is None:
        return None

    format_name = format_node.get("name", "")
    data_node = find_child(format_node, "dir", "MIP0") or format_node
    palette = find_texture_file(format_node, "Palette RGB 888") or find_texture_file(data_node, "Palette RGB 888")
    indices = find_texture_file(data_node, "Image indices")
    colors = find_texture_file(data_node, "Image colors")
    alpha = find_texture_file(data_node, "Alpha 8 bit") or find_texture_file(data_node, "Image Alpha 8 bit")

    rgba = None
    alpha_mode = "source" if alpha else "luminance"
    if is_indexed_texture_format(format_name) and palette and indices:
        rgba = palette8_to_rgba(indices, palette, width, height, alpha)
    elif format_name.lower() == "true rgb 565" and colors:
        rgba = rgb565_to_rgba(colors, alpha, width, height)
    elif format_name.lower() == "true 8 bit" and colors:
        rgba = true8_to_rgba(colors, alpha, width, height)
    elif format_name.startswith("Format_TRUE_") and colors:
        rgba = format_true_to_rgba(format_name, colors, alpha, width, height)

    if rgba is None:
        return None

    return {
        "name": chosen.get("name", texture_name or "particle_texture.bmp"),
        "width": width,
        "height": height,
        "rgba": rgba,
        "alpha": alpha_mode,
    }


def choose_texture(textures: list[ET.Element], texture_name: str) -> ET.Element:
    wanted = (texture_name or "").lower()
    wanted_stem = Path(wanted).stem
    for texture in textures:
        name = (texture.get("name", "") or "").lower()
        if name == wanted or Path(name).stem == wanted_stem:
            return texture
    return textures[0]


def find_texture_format_node(node: ET.Element | None) -> ET.Element | None:
    if node is None:
        return None

    name = node.get("name", "")
    lower = name.lower()
    if lower in {"palette 8 bit", "true rgb 565", "true 8 bit"} or name.startswith("Format_"):
        return node

    for child in list(node):
        if child.tag != "dir":
            continue
        found = find_texture_format_node(child)
        if found is not None:
            return found
    return None


def find_texture_file(node: ET.Element | None, file_name: str) -> bytes | None:
    if node is None:
        return None

    for child in list(node):
        if child.tag == "file" and child.get("name", "").lower() == file_name.lower() and child.text:
            return base64.b64decode("".join(child.text.split()))

    for child in list(node):
        if child.tag == "dir":
            found = find_texture_file(child, file_name)
            if found is not None:
                return found
    return None


def palette8_to_rgba(indices: bytes, palette: bytes, width: int, height: int, alpha: bytes | None) -> bytes:
    out = bytearray(width * height * 4)
    pixel_count = min(len(indices), width * height)
    for i in range(pixel_count):
        p = indices[i] * 3
        r = palette[p] if p < len(palette) else 0
        g = palette[p + 1] if p + 1 < len(palette) else 0
        b = palette[p + 2] if p + 2 < len(palette) else 0
        a = alpha[i] if alpha and i < len(alpha) else max(r, g, b)
        write_rgba(out, i, r, g, b, a)
    return bytes(out)


def rgb565_to_rgba(colors: bytes, alpha: bytes | None, width: int, height: int) -> bytes:
    out = bytearray(width * height * 4)
    pixel_count = min(len(colors) // 2, width * height)
    for i in range(pixel_count):
        value = struct.unpack_from("<H", colors, i * 2)[0]
        r = round(((value >> 11) & 0x1F) * 255 / 31)
        g = round(((value >> 5) & 0x3F) * 255 / 63)
        b = round((value & 0x1F) * 255 / 31)
        a = alpha[i] if alpha and i < len(alpha) else max(r, g, b)
        write_rgba(out, i, r, g, b, a)
    return bytes(out)


def true8_to_rgba(colors: bytes, alpha: bytes | None, width: int, height: int) -> bytes:
    out = bytearray(width * height * 4)
    pixel_count = min(len(colors), width * height)
    for i in range(pixel_count):
        value = colors[i]
        a = alpha[i] if alpha and i < len(alpha) else value
        write_rgba(out, i, value, value, value, a)
    return bytes(out)


def format_true_to_rgba(format_name: str, colors: bytes, alpha: bytes | None, width: int, height: int) -> bytes | None:
    bits = parse_format_true_bits(format_name)
    if bits is None:
        return None

    r_bits, g_bits, b_bits, a_bits = bits
    bits_per_pixel = r_bits + g_bits + b_bits + a_bits
    bytes_per_pixel = math.ceil(bits_per_pixel / 8)
    out = bytearray(width * height * 4)
    pixel_count = min(len(colors) // bytes_per_pixel, width * height)

    for i in range(pixel_count):
        value = int.from_bytes(colors[i * bytes_per_pixel : (i + 1) * bytes_per_pixel], "little")
        b_mask = (1 << b_bits) - 1
        g_mask = (1 << g_bits) - 1
        r_mask = (1 << r_bits) - 1
        a_mask = (1 << a_bits) - 1 if a_bits else 0
        b = value & b_mask if b_bits else 0
        g = (value >> b_bits) & g_mask if g_bits else 0
        r = (value >> (b_bits + g_bits)) & r_mask if r_bits else 0
        embedded_alpha = (value >> (b_bits + g_bits + r_bits)) & a_mask if a_bits else None
        rr = expand_bits(r, r_bits)
        gg = expand_bits(g, g_bits)
        bb = expand_bits(b, b_bits)
        aa = alpha[i] if alpha and i < len(alpha) else (expand_bits(embedded_alpha, a_bits) if embedded_alpha is not None else max(rr, gg, bb))
        write_rgba(out, i, rr, gg, bb, aa)
    return bytes(out)


def parse_format_true_bits(format_name: str) -> tuple[int, int, int, int] | None:
    try:
        parts = [int(part) for part in format_name.replace("Format_TRUE_", "").split("_") if part]
    except ValueError:
        return None
    if not parts:
        return None
    component_count = parts[0]
    sizes = parts[1 : 1 + component_count]
    if len(sizes) < component_count:
        return None
    if component_count == 2:
        return sizes[0], 0, 0, sizes[1]
    return (
        sizes[0] if len(sizes) > 0 else 0,
        sizes[1] if len(sizes) > 1 else 0,
        sizes[2] if len(sizes) > 2 else 0,
        sizes[3] if len(sizes) > 3 else 0,
    )


def is_indexed_texture_format(format_name: str) -> bool:
    lower = format_name.lower()
    return lower == "palette 8 bit" or format_name.startswith("Format_PAL8")


def expand_bits(value: int | None, bits: int) -> int:
    if not value or not bits:
        return 0
    return round(value * 255 / ((1 << bits) - 1))


def write_rgba(out: bytearray, index: int, r: int, g: int, b: int, a: int) -> None:
    offset = index * 4
    out[offset] = clamp_byte(r)
    out[offset + 1] = clamp_byte(g)
    out[offset + 2] = clamp_byte(b)
    out[offset + 3] = clamp_byte(a)


def clamp_byte(value: int) -> int:
    return max(0, min(255, int(value)))


def encode_bmp32(width: int, height: int, rgba: bytes) -> bytes:
    row_size = width * 4
    pixel_data_size = row_size * height
    file_size = 14 + 40 + pixel_data_size
    out = bytearray()
    out.extend(b"BM")
    out.extend(struct.pack("<IHHI", file_size, 0, 0, 54))
    out.extend(struct.pack("<IiiHHIIiiII", 40, width, height, 1, 32, 0, pixel_data_size, 2835, 2835, 0, 0))
    for y in range(height - 1, -1, -1):
        for x in range(width):
            i = (y * width + x) * 4
            out.extend((rgba[i + 2], rgba[i + 1], rgba[i], rgba[i + 3]))
    return bytes(out)


def parse_particle_system_parameters(data: bytes) -> dict:
    if len(data) < 636:
        raise ValueError(f"ParticleSystemParameters is too small: {len(data)} bytes")

    offset = 0
    p = default_parameters()
    p["pspFlags"] = u32(data, offset)
    offset += 4

    p["colorFrames"] = []
    for _ in range(PSP_NUM_COLOR_KEYS):
        p["colorFrames"].append({"r": f32(data, offset), "g": f32(data, offset + 4), "b": f32(data, offset + 8), "a": f32(data, offset + 12)})
        offset += 16

    p["colorKeyFrameBits"] = u32(data, offset)
    offset += 4
    p["textureName"] = cstring(data, offset, PSP_TEXTURE_NAME_LEN)
    offset += PSP_TEXTURE_NAME_LEN
    p["textureFps"] = f32(data, offset)
    offset += 4
    p["srcBlend"] = u32(data, offset)
    offset += 4
    p["dstBlend"] = u32(data, offset)
    offset += 4
    p["gravity"] = vec3(data, offset)
    offset += 12
    p["emitterDirection"] = vec3(data, offset)
    offset += 12
    p["emitterNozzleSize"] = f32(data, offset)
    offset += 4
    p["emitterNozzleDamp"] = vec3(data, offset)
    offset += 12
    p["initialParticleCount"] = i32(data, offset)
    offset += 4
    p["maxParticleCount"] = i32(data, offset)
    offset += 4
    p["lifetime"] = f32(data, offset)
    offset += 4
    p["frequency"] = f32(data, offset)
    offset += 4
    p["particleLifetime"] = f32(data, offset)
    offset += 4
    p["particlePositionRandomizer"] = f32(data, offset)
    offset += 4
    p["particleVelocity"] = f32(data, offset)
    offset += 4
    p["particleVelocityRandomizer"] = f32(data, offset)
    offset += 4
    p["particleTwistVelocity"] = f32(data, offset)
    offset += 4
    p["particleSize"] = f32(data, offset)
    offset += 4
    p["particleSizeVelocity"] = f32(data, offset)
    offset += 4
    p["boundingSphereRadius"] = f32(data, offset)
    return p


def parse_legacy_event_def(data: bytes) -> dict:
    if len(data) < 176:
        raise ValueError(f"EventDef is too small: {len(data)} bytes")

    p = default_parameters()
    alpha = f32(data, 0)
    alpha_decay = f32(data, 4)
    color = vec3(data, 16)
    color_velocity = vec3(data, 40)
    gravity = f32(data, 56)
    lifetime_ms = i32(data, 60)
    part_life_ms = u32(data, 92)
    direction = vec3(data, 156)
    if vector_length_sq(direction) <= 0.000001:
        direction = {"x": 1.0, "y": 1.0, "z": 1.0}

    p["lifetime"] = lifetime_ms * 0.001
    p["frequency"] = f32(data, 52)
    p["initialParticleCount"] = f32(data, 88)
    p["maxParticleCount"] = i32(data, 64)
    p["emitterDirection"] = direction
    p["emitterNozzleSize"] = f32(data, 72)
    p["emitterNozzleDamp"] = vec3(data, 76)
    p["gravity"] = {"x": 0.0, "y": 0.0, "z": gravity * 1000.0}
    p["particleLifetime"] = part_life_ms * 0.001
    p["particlePositionRandomizer"] = f32(data, 100)
    p["particleSize"] = f32(data, 104)
    p["particleSizeVelocity"] = f32(data, 108) * 1000.0
    p["particleTwistVelocity"] = f32(data, 132) * 1000.0
    p["particleVelocity"] = f32(data, 140) * 1000.0
    p["particleVelocityRandomizer"] = f32(data, 144)
    p["textureName"] = cstring(data, 116, PSP_TEXTURE_NAME_LEN)
    p["textureFps"] = f32(data, 172)
    p["boundingSphereRadius"] = f32(data, 152)

    if data[148]:
        p["pspFlags"] |= PSP_F_RENDER_DITHER

    legacy_bitfields = u32(data, 168)
    if legacy_bitfields & 1:
        p["pspFlags"] |= PSP_F_RELATIVE_VELOCITY
    if legacy_bitfields & 2:
        p["pspFlags"] |= PSP_F_RELATIVE_TRANSFORM

    if len(data) >= 204:
        src_blend = u32(data, 176)
        dst_blend = u32(data, 180)
        gravity_vec = vec3(data, 184)
        if src_blend > 0:
            p["srcBlend"] = src_blend
        if dst_blend > 0:
            p["dstBlend"] = dst_blend
        p["gravity"] = {
            "x": gravity_vec["x"] * 1000.0,
            "y": gravity_vec["y"] * 1000.0,
            "z": (gravity_vec["z"] + gravity) * 1000.0,
        }
        if i32(data, 196):
            p["pspFlags"] |= PSP_F_RENDER_PARTICLE_LIFE
        if i32(data, 200):
            p["pspFlags"] |= PSP_F_RENDER_FOG

    if len(data) >= 720 and u32(data, 204):
        p["colorKeyFrameBits"] = u32(data, 204) | 0x80000001
        p["colorFrames"] = []
        for index in range(PSP_NUM_COLOR_KEYS):
            offset = 208 + index * 16
            p["colorFrames"].append({"r": f32(data, offset), "g": f32(data, offset + 4), "b": f32(data, offset + 8), "a": f32(data, offset + 12)})
    elif vector_length_sq(color_velocity) > 0.0:
        duration_ms = part_life_ms or lifetime_ms or 1000
        frame_step_ms = duration_ms / PSP_NUM_COLOR_KEYS
        r, g, b, a = color["x"], color["y"], color["z"], alpha
        p["colorFrames"] = []
        for _ in range(PSP_NUM_COLOR_KEYS):
            p["colorFrames"].append({"r": clamp(r, 0.0, 1.0), "g": clamp(g, 0.0, 1.0), "b": clamp(b, 0.0, 1.0), "a": clamp(a, 0.0, 1.0)})
            r += frame_step_ms * 0.001 * color_velocity["x"]
            g += frame_step_ms * 0.001 * color_velocity["y"]
            b += frame_step_ms * 0.001 * color_velocity["z"]
            a += frame_step_ms * 0.001 * alpha_decay
    else:
        p["colorFrames"] = [{"r": color["x"], "g": color["y"], "b": color["z"], "a": alpha} for _ in range(PSP_NUM_COLOR_KEYS)]

    return p


def default_parameters() -> dict:
    return {
        "pspFlags": 0,
        "colorFrames": [{"r": 1.0, "g": 1.0, "b": 1.0, "a": 1.0} for _ in range(PSP_NUM_COLOR_KEYS)],
        "colorKeyFrameBits": 0x80000001,
        "textureName": "",
        "textureFps": 0.0,
        "srcBlend": D3DBLEND_ONE,
        "dstBlend": D3DBLEND_ONE,
        "gravity": {"x": 0.0, "y": 0.0, "z": 0.0},
        "emitterDirection": {"x": 1.0, "y": 1.0, "z": 1.0},
        "emitterNozzleSize": 0.0,
        "emitterNozzleDamp": {"x": 0.0, "y": 0.0, "z": 0.0},
        "initialParticleCount": 0,
        "maxParticleCount": 0,
        "lifetime": 0.0,
        "frequency": 0.0,
        "particleLifetime": 0.0,
        "particlePositionRandomizer": 0.0,
        "particleVelocity": 0.0,
        "particleVelocityRandomizer": 0.0,
        "particleTwistVelocity": 0.0,
        "particleSize": 0.0,
        "particleSizeVelocity": 0.0,
        "boundingSphereRadius": 0.0,
    }


def read_legacy_notes(event_dir: ET.Element) -> dict:
    notes = {}
    for name in ["Scale", "Visual Scale"]:
        node = find_child(event_dir, "file", name)
        if node is not None and node.text:
            data = decode_file(node)
            if len(data) >= 4:
                notes[name.replace(" ", "")] = f32(data, 0)
    return notes


def find_child(parent: ET.Element | None, tag: str, name: str) -> ET.Element | None:
    if parent is None:
        return None
    for child in list(parent):
        if child.tag == tag and child.get("name") == name:
            return child
    return None


def decode_file(element: ET.Element) -> bytes:
    text = "".join((element.text or "").split())
    return base64.b64decode(text)


def f32(data: bytes, offset: int) -> float:
    return struct.unpack_from("<f", data, offset)[0]


def i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def bytes_i32(data: bytes) -> int:
    if len(data) < 4:
        return 0
    return struct.unpack_from("<i", data, 0)[0]


def vec3(data: bytes, offset: int) -> dict:
    return {"x": f32(data, offset), "y": f32(data, offset + 4), "z": f32(data, offset + 8)}


def cstring(data: bytes, offset: int, max_length: int) -> str:
    raw = data[offset : offset + max_length]
    return raw.split(b"\0", 1)[0].decode("ascii", errors="replace")


def vector_length_sq(vector: dict) -> float:
    return vector["x"] * vector["x"] + vector["y"] * vector["y"] + vector["z"] * vector["z"]


def clamp(value: float, min_value: float, max_value: float) -> float:
    return max(min_value, min(max_value, value))


def bool_text(value: bool | int) -> str:
    return "true" if bool(value) else "false"


def fmt(value: float | int) -> str:
    if isinstance(value, int):
        return str(value)
    if not math.isfinite(value):
        return "0"
    return f"{value:.9g}"


def indent(element: ET.Element, level: int = 0) -> None:
    whitespace = "\n" + level * "  "
    child_whitespace = "\n" + (level + 1) * "  "
    if len(element):
        if not element.text or not element.text.strip():
            element.text = child_whitespace
        for child in element:
            indent(child, level + 1)
        if not element[-1].tail or not element[-1].tail.strip():
            element[-1].tail = whitespace
    if level and (not element.tail or not element.tail.strip()):
        element.tail = whitespace


if __name__ == "__main__":
    raise SystemExit(main())
