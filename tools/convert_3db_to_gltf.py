#!/usr/bin/env python3
"""Convert one Conquest Frontier Wars .3db.xml dump to glTF 2.0 JSON.

This intentionally supports only single openFLAME 3D N-mesh .3db XML dumps.
Particles, shields, compound .cmp assembly, and animation are separate formats
and are left for later converter stages.
"""

from __future__ import annotations

import argparse
import base64
import json
import math
import re
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable
from xml.etree import ElementTree as ET


FACE_TWO_SIDED = 0x01
FACE_FLAT_SHADED = 0x02
FACE_HIDDEN = 0x08

COMPONENT_FLOAT = 5126
COMPONENT_UNSIGNED_BYTE = 5121


@dataclass
class TextureImage:
    name: str
    width: int
    height: int
    rgba: bytes
    has_alpha: bool
    uri: str = ""


@dataclass
class MaterialInfo:
    name: str
    diffuse: tuple[float, float, float]
    emission: tuple[float, float, float]
    specular: tuple[float, float, float]
    transparency: float
    shininess: tuple[float, ...]
    diffuse_texture_name: str | None
    diffuse_texture_flags: int
    identifier: int


@dataclass
class Accessor:
    index: int
    count: int


class BinWriter:
    def __init__(self) -> None:
        self.data = bytearray()

    def add_accessor(
        self,
        gltf: dict,
        values: bytes,
        *,
        component_type: int,
        type_name: str,
        count: int,
        target: int | None = None,
        min_value: list[float] | None = None,
        max_value: list[float] | None = None,
        normalized: bool = False,
    ) -> int:
        align = 4
        pad_len = (-len(self.data)) % align
        if pad_len:
            self.data.extend(b"\x00" * pad_len)

        offset = len(self.data)
        self.data.extend(values)

        view = {
            "buffer": 0,
            "byteOffset": offset,
            "byteLength": len(values),
        }
        if target is not None:
            view["target"] = target

        buffer_view_index = len(gltf["bufferViews"])
        gltf["bufferViews"].append(view)

        accessor = {
            "bufferView": buffer_view_index,
            "byteOffset": 0,
            "componentType": component_type,
            "count": count,
            "type": type_name,
        }
        if normalized:
            accessor["normalized"] = True
        if min_value is not None:
            accessor["min"] = min_value
        if max_value is not None:
            accessor["max"] = max_value

        accessor_index = len(gltf["accessors"])
        gltf["accessors"].append(accessor)
        return accessor_index


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert a single .3db.xml UTF dump to glTF 2.0 JSON."
    )
    parser.add_argument("input", type=Path, help="Input .3db.xml file")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="Output .gltf path. Defaults to <input-stem>.gltf beside the input.",
    )
    parser.add_argument(
        "--no-textures",
        action="store_true",
        help="Do not write decoded PNG textures or bind baseColorTexture.",
    )
    parser.add_argument(
        "--uv-transform",
        choices=("none", "flip-v", "flip-u", "flip-uv", "swap", "swap-flip-u", "swap-flip-v", "swap-flip-uv"),
        default="none",
        help="Texture coordinate transform to apply. Default preserves source UVs.",
    )
    args = parser.parse_args()

    input_path = args.input
    if not input_path.exists():
        raise SystemExit(f"Input file does not exist: {input_path}")

    output_path = args.output or input_path.with_suffix("").with_suffix(".gltf")
    output_path.parent.mkdir(parents=True, exist_ok=True)

    root = ET.parse(input_path).getroot()
    if root.tag != "unit":
        raise SystemExit(f"Expected <unit> root in {input_path}")

    mesh_root = child_dir(root, "openFLAME 3D N-mesh")
    if mesh_root is None:
        raise SystemExit("No openFLAME 3D N-mesh directory found. This converter only handles single .3db meshes.")

    textures = {} if args.no_textures else load_textures(mesh_root)
    write_textures(textures, output_path.parent)
    materials, id_to_index = load_materials(mesh_root, textures)
    gltf, bin_data = build_gltf(root, mesh_root, materials, id_to_index, textures, output_path, args.uv_transform)

    bin_path = output_path.with_suffix(".bin")
    gltf["buffers"][0]["uri"] = bin_path.name
    gltf["buffers"][0]["byteLength"] = len(bin_data)

    bin_path.write_bytes(bin_data)
    output_path.write_text(json.dumps(gltf, indent=2), encoding="utf-8")

    print(f"Wrote {output_path}")
    print(f"Wrote {bin_path}")
    if textures:
        print(f"Wrote {len(textures)} texture PNG(s)")
    return 0


def build_gltf(
    unit_root: ET.Element,
    mesh_root: ET.Element,
    materials: list[MaterialInfo],
    id_to_index: dict[int, int],
    textures: dict[str, TextureImage],
    output_path: Path,
    uv_transform: str,
) -> tuple[dict, bytes]:
    gltf = {
        "asset": {
            "version": "2.0",
            "generator": "Conquest Frontier Wars 3db XML converter",
        },
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": sanitize_name(unit_root.attrib.get("name", output_path.stem)), "mesh": 0}],
        "meshes": [{"name": sanitize_name(unit_root.attrib.get("name", output_path.stem)), "primitives": []}],
        "materials": [],
        "buffers": [{"byteLength": 0, "uri": ""}],
        "bufferViews": [],
        "accessors": [],
        "images": [],
        "textures": [],
        "samplers": [],
    }

    texture_index_by_name = add_gltf_textures(gltf, textures)
    material_variant_cache: dict[tuple[int, bool], int] = {}

    writer = BinWriter()
    mesh_children = child_dirs(mesh_root)
    vertices_node = mesh_children.get("Vertices")
    normals_node = mesh_children.get("Normals")
    face_groups_node = mesh_children.get("Face groups")
    if vertices_node is None or face_groups_node is None:
        raise SystemExit("Mesh is missing Vertices or Face groups.")

    vertices_children = child_dirs(vertices_node)
    normals_children = child_dirs(normals_node) if normals_node is not None else {}
    groups_children = child_dirs(face_groups_node)

    object_vertices = vector3_array(float_array(file_bytes(vertices_children.get("Object vertex list"))))
    tex_vertices = uv_array(float_array(file_bytes(vertices_children.get("Texture vertex list"))))
    surface_normals = vector3_array(float_array(file_bytes(normals_children.get("Surface normal list"))))
    vertex_batch = int_array(file_bytes(vertices_children.get("Vertex batch list")))
    texture_batch = int_array(file_bytes(vertices_children.get("Texture batch list")))
    texture_batch2 = int_array(file_bytes(vertices_children.get("Texture batch list2")))
    vertex_normals = int_array(file_bytes(vertices_children.get("Vertex normal")))
    vertex_colors = file_bytes(vertices_children.get("Color")) or b""

    if not object_vertices:
        raise SystemExit("Object vertex list is empty.")

    for group_name in sorted((name for name in groups_children if name.startswith("Group")), key=natural_key):
        group = groups_children[group_name]
        g = child_dirs(group)
        face_chain = int_array(file_bytes(g.get("Face vertex chain")))
        face_normals = int_array(file_bytes(g.get("Face normal")))
        face_properties = int_array(file_bytes(g.get("Face property")))
        face_count = int32(file_bytes(g.get("Face count"))) or (len(face_chain) // 3)
        material_id = int32(file_bytes(g.get("Material"))) or 0
        material_index = id_to_index.get(material_id, material_id)
        double_sided = any((prop & FACE_TWO_SIDED) for prop in face_properties[:face_count])

        positions: list[float] = []
        normals: list[float] = []
        uvs: list[float] = []
        colors: list[int] = []
        has_vertex_colors = len(vertex_colors) >= len(object_vertices) * 3

        for face_index in range(face_count):
            prop = face_properties[face_index] if face_index < len(face_properties) else 0x04
            if prop & FACE_HIDDEN:
                continue

            flat = bool(prop & FACE_FLAT_SHADED)
            face_normal_index = face_normals[face_index] if face_index < len(face_normals) else 0
            face_normal = vector_at(surface_normals, face_normal_index)

            for vertex_in_face in range(3):
                chain_index = face_chain[face_index * 3 + vertex_in_face]
                object_index = vertex_batch[chain_index] if chain_index < len(vertex_batch) else 0
                tex_index = texture_batch[chain_index] if chain_index < len(texture_batch) else 0
                position = vector_at(object_vertices, object_index)
                normal_index = vertex_normals[object_index] if object_index < len(vertex_normals) else 0
                normal = face_normal if flat else vector_at(surface_normals, normal_index)
                uv = transform_uv(tex_at(tex_vertices, tex_index), uv_transform)

                positions.extend(position)
                normals.extend(normal)
                uvs.extend(uv)
                if has_vertex_colors:
                    colors.extend(vertex_color(vertex_colors, object_index))

        vertex_count = len(positions) // 3
        if vertex_count == 0:
            continue

        pos_min, pos_max = min_max_vec3(positions)
        attrs = {
            "POSITION": writer.add_accessor(
                gltf,
                pack_floats(positions),
                component_type=COMPONENT_FLOAT,
                type_name="VEC3",
                count=vertex_count,
                target=34962,
                min_value=pos_min,
                max_value=pos_max,
            ),
            "NORMAL": writer.add_accessor(
                gltf,
                pack_floats(normals),
                component_type=COMPONENT_FLOAT,
                type_name="VEC3",
                count=vertex_count,
                target=34962,
            ),
            "TEXCOORD_0": writer.add_accessor(
                gltf,
                pack_floats(uvs),
                component_type=COMPONENT_FLOAT,
                type_name="VEC2",
                count=vertex_count,
                target=34962,
            ),
        }

        if has_vertex_colors:
            attrs["COLOR_0"] = writer.add_accessor(
                gltf,
                bytes(colors),
                component_type=COMPONENT_UNSIGNED_BYTE,
                type_name="VEC3",
                count=vertex_count,
                target=34962,
                normalized=True,
            )

        primitive = {
            "attributes": attrs,
            "mode": 4,
            "material": material_variant(
                gltf,
                materials,
                material_index,
                double_sided,
                textures,
                texture_index_by_name,
                material_variant_cache,
            ),
            "extras": {
                "sourceFaceGroup": group_name,
                "sourceMaterialId": material_id,
            },
        }
        gltf["meshes"][0]["primitives"].append(primitive)

    if not gltf["meshes"][0]["primitives"]:
        raise SystemExit("No renderable face groups were found.")

    add_hardpoint_nodes(gltf, unit_root)
    prune_empty_gltf_arrays(gltf)
    return gltf, bytes(writer.data)


def add_gltf_textures(gltf: dict, textures: dict[str, TextureImage]) -> dict[str, int]:
    texture_index_by_name: dict[str, int] = {}
    for tex in textures.values():
        image_index = len(gltf["images"])
        gltf["images"].append({"name": tex.name, "uri": tex.uri})

        sampler_index = len(gltf["samplers"])
        gltf["samplers"].append({"magFilter": 9728, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497})

        texture_index = len(gltf["textures"])
        gltf["textures"].append({"sampler": sampler_index, "source": image_index, "name": tex.name})
        texture_index_by_name[tex.name.lower()] = texture_index
        texture_index_by_name[Path(tex.name).stem.lower()] = texture_index
    return texture_index_by_name


def material_variant(
    gltf: dict,
    materials: list[MaterialInfo],
    material_index: int,
    double_sided: bool,
    textures: dict[str, TextureImage],
    texture_index_by_name: dict[str, int],
    cache: dict[tuple[int, bool], int],
) -> int:
    key = (material_index, double_sided)
    if key in cache:
        return cache[key]

    mat = materials[material_index] if 0 <= material_index < len(materials) else default_material()
    alpha = max(0.0, min(1.0, mat.transparency))
    pbr = {
        "baseColorFactor": [mat.diffuse[0], mat.diffuse[1], mat.diffuse[2], alpha],
        "metallicFactor": 0.0,
        "roughnessFactor": shininess_to_roughness(mat.shininess),
    }

    texture_index = texture_lookup(texture_index_by_name, mat.diffuse_texture_name)
    texture_has_alpha = texture_name_has_alpha(textures, mat.diffuse_texture_name)
    if texture_index is not None:
        pbr["baseColorTexture"] = {"index": texture_index}

    out = {
        "name": mat.name + ("_doubleSided" if double_sided else ""),
        "pbrMetallicRoughness": pbr,
        "doubleSided": double_sided,
        "extras": {
            "sourceMaterialIdentifier": mat.identifier,
            "sourceSpecular": list(mat.specular),
            "sourceDiffuseTextureFlags": mat.diffuse_texture_flags,
        },
    }
    if any(mat.emission):
        out["emissiveFactor"] = list(mat.emission)
    if alpha < 1.0 or texture_has_alpha:
        out["alphaMode"] = "BLEND"
        out["alphaCutoff"] = 0.01

    index = len(gltf["materials"])
    gltf["materials"].append(out)
    cache[key] = index
    return index


def load_materials(mesh_root: ET.Element, textures: dict[str, TextureImage]) -> tuple[list[MaterialInfo], dict[int, int]]:
    material_library = child_dir(mesh_root, "Material library")
    if material_library is None:
        return [default_material()], {0: 0}

    materials: list[MaterialInfo] = []
    id_to_index: dict[int, int] = {}
    for mat_node in sorted(
        [child for child in material_library if child.tag == "dir"],
        key=lambda node: natural_key(node.attrib.get("name", "")),
    ):
        children = child_dirs(mat_node)
        diffuse_node = children.get("Diffuse")
        emission_node = children.get("Emission")
        shininess = tuple(float_array(file_bytes(child_file(child_dirs(children.get("Shininess")), "Constant"))))
        transparency_values = float_array(file_bytes(child_file(child_dirs(children.get("Transparency")), "Constant")))
        diffuse_texture_name = map_name(diffuse_node)
        texture_flags = int32(file_bytes(child_file(child_dirs(child_dirs(diffuse_node).get("Map")), "Flags"))) or 0
        identifier = int32(file_bytes(children.get("Material identifier"))) or len(materials)
        material = MaterialInfo(
            name=mat_node.attrib.get("name", f"Material_{len(materials)}"),
            diffuse=read_color(child_file(child_dirs(diffuse_node), "Constant"), (1.0, 1.0, 1.0)),
            emission=read_color(child_file(child_dirs(emission_node), "Constant"), (0.0, 0.0, 0.0)),
            specular=read_color(child_file(child_dirs(children.get("Specular")), "Constant"), (0.08, 0.08, 0.08)),
            transparency=transparency_values[0] if transparency_values else 1.0,
            shininess=shininess,
            diffuse_texture_name=diffuse_texture_name,
            diffuse_texture_flags=texture_flags,
            identifier=identifier,
        )
        id_to_index[identifier] = len(materials)
        materials.append(material)

    if not materials:
        materials.append(default_material())
        id_to_index[0] = 0
    return materials, id_to_index


def load_textures(mesh_root: ET.Element) -> dict[str, TextureImage]:
    texture_library = child_dir(mesh_root, "Texture library")
    if texture_library is None:
        return {}

    textures: dict[str, TextureImage] = {}
    for tex_node in [child for child in texture_library if child.tag == "dir"]:
        texture = decode_texture_node(tex_node.attrib.get("name", "texture"), tex_node)
        if texture is not None:
            textures[texture.name] = texture
    return textures


def decode_texture_node(tex_name: str, tex_node: ET.Element) -> TextureImage | None:
    mip_node = find_mip0_node(tex_node)
    format_node = find_texture_format_node(tex_node)
    if format_node is None:
        format_node = find_texture_format_node(mip_node)
    if format_node is None:
        return None

    width = int32(file_bytes(find_child_ci(tex_node, "Image X size"))) or int32(file_bytes(find_child_ci(mip_node, "Image X size")))
    height = int32(file_bytes(find_child_ci(tex_node, "Image Y size"))) or int32(file_bytes(find_child_ci(mip_node, "Image Y size")))
    if not width or not height:
        return None

    data_node = find_child_ci(format_node, "MIP0")
    if data_node is None:
        data_node = format_node
    palette = file_bytes(find_child_ci(format_node, "Palette RGB 888"))
    if not palette:
        palette = file_bytes(find_child_ci(data_node, "Palette RGB 888"))
    indices = file_bytes(find_child_ci(data_node, "Image indices"))
    colors = file_bytes(find_child_ci(data_node, "Image colors"))
    alpha = file_bytes(find_child_ci(data_node, "Alpha 8 bit")) or file_bytes(find_child_ci(data_node, "Image Alpha 8 bit"))
    format_name = format_node.attrib.get("name", "")

    rgba: bytes | None = None
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

    return TextureImage(tex_name, width, height, rgba, bool(alpha) or has_non_opaque_alpha(rgba))


def write_textures(textures: dict[str, TextureImage], out_dir: Path) -> None:
    used_names: set[str] = set()
    for texture in textures.values():
        file_name = safe_file_stem(texture.name) + ".png"
        base = file_name
        suffix = 1
        while file_name.lower() in used_names:
            file_name = f"{Path(base).stem}_{suffix}.png"
            suffix += 1
        used_names.add(file_name.lower())
        texture.uri = file_name
        (out_dir / file_name).write_bytes(write_png_rgba(texture.width, texture.height, texture.rgba))


def add_hardpoint_nodes(gltf: dict, unit_root: ET.Element) -> None:
    hardpoints = child_dir(unit_root, "Hardpoints")
    fixed = child_dir(hardpoints, "Fixed") if hardpoints is not None else None
    if fixed is None:
        return

    parent_children = gltf["nodes"][0].setdefault("children", [])
    for hp in [child for child in fixed if child.tag == "dir"]:
        children = child_dirs(hp)
        position = vector_at(vector3_array(float_array(file_bytes(children.get("Position")))), 0)
        orientation = float_array(file_bytes(children.get("Orientation")))
        node = {
            "name": hp.attrib.get("name", "hardpoint"),
            "translation": list(position),
            "extras": {
                "sourceType": "hardpoint",
                "sourceOrientation3x3": orientation,
            },
        }
        parent_children.append(len(gltf["nodes"]))
        gltf["nodes"].append(node)


def child_dirs(node: ET.Element | None) -> dict[str, ET.Element]:
    if node is None:
        return {}
    return {child.attrib.get("name", ""): child for child in node if child.tag in {"dir", "file"}}


def child_dir(node: ET.Element | None, name: str) -> ET.Element | None:
    if node is None:
        return None
    for child in node:
        if child.tag == "dir" and child.attrib.get("name") == name:
            return child
    return None


def child_file(children: dict[str, ET.Element], name: str) -> ET.Element | None:
    node = children.get(name)
    return node if node is not None and node.tag == "file" else None


def find_child_ci(node: ET.Element | None, name: str) -> ET.Element | None:
    if node is None:
        return None
    lower = name.lower()
    for child in node:
        if child.attrib.get("name", "").lower() == lower:
            return child
    return None


def find_mip0_node(node: ET.Element | None) -> ET.Element | None:
    direct = find_child_ci(node, "MIP0")
    if direct is not None:
        return direct
    if node is None:
        return None
    for child in node:
        nested = find_child_ci(child, "MIP0")
        if nested is not None:
            return nested
    return None


def find_texture_format_node(node: ET.Element | None) -> ET.Element | None:
    if node is None:
        return None
    for child in node:
        name = child.attrib.get("name", "")
        lower = name.lower()
        if lower in {"palette 8 bit", "true rgb 565", "true 8 bit"} or name.startswith("Format_"):
            return child
    return None


def file_bytes(node: ET.Element | None) -> bytes | None:
    if node is None or node.tag != "file":
        return None
    text = re.sub(r"\s+", "", node.text or "")
    return base64.b64decode(text) if text else b""


def int32(data: bytes | None) -> int | None:
    if not data or len(data) < 4:
        return None
    return struct.unpack_from("<i", data, 0)[0]


def int_array(data: bytes | None) -> list[int]:
    if not data:
        return []
    count = len(data) // 4
    return list(struct.unpack("<" + "i" * count, data[: count * 4]))


def float_array(data: bytes | None) -> list[float]:
    if not data:
        return []
    count = len(data) // 4
    return list(struct.unpack("<" + "f" * count, data[: count * 4]))


def vector3_array(values: list[float]) -> list[tuple[float, float, float]]:
    return [(values[i], values[i + 1], values[i + 2]) for i in range(0, len(values) - 2, 3)]


def uv_array(values: list[float]) -> list[tuple[float, float]]:
    return [(values[i], values[i + 1]) for i in range(0, len(values) - 1, 2)]


def vector_at(values: list[tuple[float, float, float]], index: int) -> tuple[float, float, float]:
    if 0 <= index < len(values):
        return values[index]
    return (0.0, 0.0, 0.0)


def tex_at(values: list[tuple[float, float]], index: int) -> tuple[float, float]:
    if 0 <= index < len(values):
        u, v = values[index]
    else:
        u, v = 0.0, 0.0
    return (u, v)


def transform_uv(uv: tuple[float, float], transform: str) -> tuple[float, float]:
    u, v = uv
    if transform.startswith("swap"):
        u, v = v, u

    if transform in {"flip-u", "flip-uv", "swap-flip-u", "swap-flip-uv"}:
        u = 1.0 - u
    if transform in {"flip-v", "flip-uv", "swap-flip-v", "swap-flip-uv"}:
        v = 1.0 - v

    return (u, v)


def vertex_color(vertex_colors: bytes, object_index: int) -> list[int]:
    offset = object_index * 3
    if offset + 2 < len(vertex_colors):
        return [vertex_colors[offset], vertex_colors[offset + 1], vertex_colors[offset + 2]]
    return [255, 255, 255]


def read_color(node: ET.Element | None, fallback: tuple[float, float, float]) -> tuple[float, float, float]:
    data = file_bytes(node)
    if not data:
        return fallback
    if len(data) >= 12:
        return tuple(max(0.0, min(1.0, v)) for v in struct.unpack_from("<fff", data, 0))
    if len(data) >= 3:
        return (data[0] / 255.0, data[1] / 255.0, data[2] / 255.0)
    return fallback


def map_name(property_node: ET.Element | None) -> str | None:
    map_node = child_dir(property_node, "Map")
    name_bytes = file_bytes(child_file(child_dirs(map_node), "Name"))
    if not name_bytes:
        return None
    return name_bytes.split(b"\x00", 1)[0].decode("ascii", errors="replace")


def default_material() -> MaterialInfo:
    return MaterialInfo("Default", (1.0, 1.0, 1.0), (0.0, 0.0, 0.0), (0.08, 0.08, 0.08), 1.0, (0.15,), None, 0, 0)


def texture_lookup(texture_index_by_name: dict[str, int], name: str | None) -> int | None:
    if not name:
        return None
    exact = texture_index_by_name.get(name.lower())
    if exact is not None:
        return exact
    return texture_index_by_name.get(Path(name).stem.lower())


def texture_name_has_alpha(textures: dict[str, TextureImage], name: str | None) -> bool:
    if not name:
        return False
    lower = name.lower()
    stem = Path(name).stem.lower()
    for tex_name, tex in textures.items():
        if tex_name.lower() == lower or Path(tex_name).stem.lower() == stem:
            return tex.has_alpha
    return False


def shininess_to_roughness(values: tuple[float, ...]) -> float:
    if not values:
        return 0.8
    shininess = max(0.0, min(1.0, values[-1]))
    return max(0.04, min(1.0, 1.0 - math.sqrt(shininess)))


def palette8_to_rgba(indices: bytes, palette: bytes, width: int, height: int, alpha: bytes | None) -> bytes:
    out = bytearray(width * height * 4)
    pixel_count = min(len(indices), width * height)
    for i in range(pixel_count):
        p = indices[i] * 3
        o = i * 4
        out[o] = palette[p] if p < len(palette) else 0
        out[o + 1] = palette[p + 1] if p + 1 < len(palette) else 0
        out[o + 2] = palette[p + 2] if p + 2 < len(palette) else 0
        out[o + 3] = alpha[i] if alpha and i < len(alpha) else 255
    return bytes(out)


def rgb565_to_rgba(colors: bytes, alpha: bytes | None, width: int, height: int) -> bytes:
    out = bytearray(width * height * 4)
    pixel_count = min(len(colors) // 2, width * height)
    for i in range(pixel_count):
        value = struct.unpack_from("<H", colors, i * 2)[0]
        o = i * 4
        out[o] = round(((value >> 11) & 0x1F) * 255 / 31)
        out[o + 1] = round(((value >> 5) & 0x3F) * 255 / 63)
        out[o + 2] = round((value & 0x1F) * 255 / 31)
        out[o + 3] = alpha[i] if alpha and i < len(alpha) else 255
    return bytes(out)


def true8_to_rgba(colors: bytes, alpha: bytes | None, width: int, height: int) -> bytes:
    out = bytearray(width * height * 4)
    pixel_count = min(len(colors), width * height)
    for i in range(pixel_count):
        o = i * 4
        out[o] = out[o + 1] = out[o + 2] = colors[i]
        out[o + 3] = alpha[i] if alpha and i < len(alpha) else 255
    return bytes(out)


def format_true_to_rgba(format_name: str, colors: bytes, alpha: bytes | None, width: int, height: int) -> bytes | None:
    bits = parse_format_true_bits(format_name)
    if bits is None:
        return None
    r_bits, g_bits, b_bits, a_bits = bits
    bits_per_pixel = sum(bits)
    bytes_per_pixel = math.ceil(bits_per_pixel / 8)
    out = bytearray(width * height * 4)
    pixel_count = min(len(colors) // bytes_per_pixel, width * height)
    for i in range(pixel_count):
        value = int.from_bytes(colors[i * bytes_per_pixel : (i + 1) * bytes_per_pixel], "little")
        b_mask = (1 << b_bits) - 1 if b_bits else 0
        g_mask = (1 << g_bits) - 1 if g_bits else 0
        r_mask = (1 << r_bits) - 1 if r_bits else 0
        a_mask = (1 << a_bits) - 1 if a_bits else 0
        b = value & b_mask if b_bits else 0
        g = (value >> b_bits) & g_mask if g_bits else 0
        r = (value >> (b_bits + g_bits)) & r_mask if r_bits else 0
        embedded_a = (value >> (b_bits + g_bits + r_bits)) & a_mask if a_bits else None
        o = i * 4
        out[o] = expand_bits(r, r_bits)
        out[o + 1] = expand_bits(g, g_bits)
        out[o + 2] = expand_bits(b, b_bits)
        out[o + 3] = alpha[i] if alpha and i < len(alpha) else (255 if embedded_a is None else expand_bits(embedded_a, a_bits))
    return bytes(out)


def parse_format_true_bits(format_name: str) -> tuple[int, int, int, int] | None:
    parts = [int(part) for part in re.sub(r"^Format_TRUE_", "", format_name).split("_") if part.isdigit()]
    if not parts:
        return None
    count = parts[0]
    sizes = parts[1 : 1 + count]
    if len(sizes) != count:
        return None
    if count == 2:
        return (sizes[0], 0, 0, sizes[1])
    return tuple((sizes + [0, 0, 0, 0])[:4])  # type: ignore[return-value]


def is_indexed_texture_format(format_name: str) -> bool:
    lower = format_name.lower()
    return lower == "palette 8 bit" or format_name.startswith("Format_PAL8")


def has_non_opaque_alpha(rgba: bytes) -> bool:
    return any(rgba[i] != 255 for i in range(3, len(rgba), 4))


def write_png_rgba(width: int, height: int, rgba: bytes) -> bytes:
    def chunk(kind: bytes, payload: bytes) -> bytes:
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)

    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        start = y * stride
        raw.extend(rgba[start : start + stride])

    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )


def pack_floats(values: Iterable[float]) -> bytes:
    values = list(values)
    return struct.pack("<" + "f" * len(values), *values)


def min_max_vec3(values: list[float]) -> tuple[list[float], list[float]]:
    xs = values[0::3]
    ys = values[1::3]
    zs = values[2::3]
    return [min(xs), min(ys), min(zs)], [max(xs), max(ys), max(zs)]


def expand_bits(value: int, bits: int) -> int:
    if not bits:
        return 0
    return round(value * 255 / ((1 << bits) - 1))


def safe_file_stem(name: str) -> str:
    stem = Path(name).stem or name
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", stem).strip("._") or "texture"


def sanitize_name(name: str) -> str:
    return name.replace("\x00", "").strip() or "Mesh"


def natural_key(value: str) -> list[object]:
    return [int(part) if part.isdigit() else part.lower() for part in re.split(r"(\d+)", value)]


def prune_empty_gltf_arrays(gltf: dict) -> None:
    for key in ["images", "textures", "samplers"]:
        if not gltf.get(key):
            gltf.pop(key, None)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except BrokenPipeError:
        raise SystemExit(1)
