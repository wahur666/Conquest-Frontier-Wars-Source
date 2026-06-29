#!/usr/bin/env python3
"""Convert Conquest Frontier Wars .3db.xml and .cmp.xml dumps to glTF 2.0 JSON.

Single openFLAME 3D N-mesh .3db XML dumps are exported as one mesh. Compound
.cmp XML dumps are exported as multiple mesh nodes wired by the CMP joint tree.
Compound animation channels are exported as glTF animation samplers where the
source channel format is understood.
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
    second_diffuse_texture_name: str | None
    second_diffuse_texture_flags: int
    emission_texture_name: str | None
    emission_texture_flags: int
    emission_texture_blend: float
    identifier: int


@dataclass
class Accessor:
    index: int
    count: int


@dataclass
class CompoundPart:
    part_dir: str
    object_name: str
    file_name: str
    index: int


@dataclass
class CompoundJoint:
    type: str
    parent: str
    child: str
    rel_position: tuple[float, float, float]
    rel_orientation: tuple[float, ...]
    parent_point: tuple[float, float, float]
    child_point: tuple[float, float, float]
    axis: tuple[float, float, float]


@dataclass
class CompoundAnimationTrack:
    script_name: str
    map_name: str
    target_name: str
    parent_name: str
    channel_name: str
    channel_type: int
    times: list[float]
    rotations: list[tuple[float, float, float, float]] | None = None
    translations: list[tuple[float, float, float]] | None = None


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
        description="Convert a .3db.xml or .cmp.xml UTF dump to glTF 2.0 JSON."
    )
    parser.add_argument("input", type=Path, help="Input .3db.xml or .cmp.xml file")
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
    parser.add_argument(
        "--scale",
        type=float,
        default=1.0,
        help="Scale factor to apply to all coordinates (e.g., 0.01 for 1/100 scale).",
    )
    parser.add_argument(
        "--write-particles",
        action="store_true",
        help="For .cmp.xml input, write <output>.particles.json with skipped .pte particle part attachments.",
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
    particle_doc = None
    if mesh_root is not None:
        textures = {} if args.no_textures else load_textures(mesh_root)
        write_textures(textures, output_path.parent)
        materials, id_to_index = load_materials(mesh_root, textures)
        gltf, bin_data = build_gltf(root, mesh_root, materials, id_to_index, textures, output_path, args.uv_transform, args.scale)
    elif child_dir(root, "Cmpnd") is not None:
        gltf, bin_data, textures, particle_doc = build_compound_gltf(
            root,
            input_path,
            output_path,
            args.no_textures,
            args.uv_transform,
            args.scale,
            write_particles=args.write_particles,
        )
    else:
        raise SystemExit("No openFLAME 3D N-mesh or Cmpnd directory found.")

    bin_path = output_path.with_suffix(".bin")
    gltf["buffers"][0]["uri"] = bin_path.name
    gltf["buffers"][0]["byteLength"] = len(bin_data)

    bin_path.write_bytes(bin_data)
    output_path.write_text(json.dumps(gltf, indent=2), encoding="utf-8")

    print(f"Wrote {output_path}")
    print(f"Wrote {bin_path}")
    if textures:
        print(f"Wrote {len(textures)} texture PNG(s)")
    if args.write_particles and particle_doc is not None and particle_doc.get("particles"):
        particles_path = output_path.with_suffix(".particles.json")
        particles_path.write_text(json.dumps(particle_doc, indent=2), encoding="utf-8")
        print(f"Wrote {particles_path}")
    return 0


def build_gltf(
    unit_root: ET.Element,
    mesh_root: ET.Element,
    materials: list[MaterialInfo],
    id_to_index: dict[int, int],
    textures: dict[str, TextureImage],
    output_path: Path,
    uv_transform: str,
    scale: float = 1.0
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
    append_mesh_primitives(
        gltf,
        writer,
        mesh_root,
        0,
        materials,
        id_to_index,
        textures,
        texture_index_by_name,
        material_variant_cache,
        args_label="mesh",
        uv_transform=uv_transform,
        scale=scale
    )

    if not gltf["meshes"][0]["primitives"]:
        raise SystemExit("No renderable face groups were found.")

    add_hardpoint_nodes(gltf, unit_root)
    prune_empty_gltf_arrays(gltf)
    return gltf, bytes(writer.data)


def build_compound_gltf(
    unit_root: ET.Element,
    input_path: Path,
    output_path: Path,
    no_textures: bool,
    uv_transform: str,
    scale: float = 1.0,
    *,
    write_particles: bool = False,
) -> tuple[dict, bytes, dict[str, TextureImage], dict | None]:
    cmpnd = child_dir(unit_root, "Cmpnd")
    if cmpnd is None:
        raise SystemExit("Compound file is missing Cmpnd directory.")

    parts = read_compound_parts(cmpnd)
    if not parts:
        raise SystemExit("Compound file has no Root/Part entries.")

    part_units = resolve_compound_part_units(unit_root, input_path, parts)
    if not part_units:
        raise SystemExit("No compound .3db parts could be resolved.")

    textures: dict[str, TextureImage] = {}
    if not no_textures:
        for name, texture in load_textures(unit_root).items():
            textures.setdefault(name, texture)
        for _, part_unit, mesh_root in part_units:
            for name, texture in load_textures(mesh_root).items():
                textures.setdefault(name, texture)
        write_textures(textures, output_path.parent)

    gltf = {
        "asset": {
            "version": "2.0",
            "generator": "Conquest Frontier Wars CMP XML converter",
        },
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": sanitize_name(unit_root.attrib.get("name", output_path.stem)), "children": []}],
        "meshes": [],
        "materials": [],
        "buffers": [{"byteLength": 0, "uri": ""}],
        "bufferViews": [],
        "accessors": [],
        "images": [],
        "textures": [],
        "samplers": [],
        "animations": [],
    }

    writer = BinWriter()
    texture_index_by_name = add_gltf_textures(gltf, textures)
    node_by_part: dict[str, int] = {}
    material_variant_cache: dict[tuple[str, int, bool], int] = {}

    for part, part_unit, mesh_root in part_units:
        mesh_index = len(gltf["meshes"])
        gltf["meshes"].append({"name": sanitize_name(part.object_name), "primitives": []})

        part_textures = textures if not no_textures else {}
        materials, id_to_index = load_materials(mesh_root, part_textures)
        append_mesh_primitives(
            gltf,
            writer,
            mesh_root,
            mesh_index,
            materials,
            id_to_index,
            part_textures,
            texture_index_by_name,
            material_variant_cache,
            args_label=part.object_name,
            uv_transform=uv_transform,
            scale=scale
        )

        if not gltf["meshes"][mesh_index]["primitives"]:
            continue

        node_index = len(gltf["nodes"])
        node_by_part[part.object_name] = node_index
        gltf["nodes"].append(
            {
                "name": sanitize_name(part.object_name),
                "mesh": mesh_index,
                "extras": {
                    "sourcePartDir": part.part_dir,
                    "sourceFileName": part.file_name,
                    "sourcePartIndex": part.index,
                    "sourceUnitName": part_unit.attrib.get("name", ""),
                },
            }
        )
        add_hardpoint_nodes_to_parent(gltf, part_unit, node_index)

    joints = read_compound_joints(child_dir(cmpnd, "Cons"))
    attach_compound_nodes(gltf, node_by_part, joints, scale)
    add_compound_animations(gltf, writer, unit_root, node_by_part, joints, scale)
    gltf["nodes"][0].setdefault("extras", {})["sourceCompoundJoints"] = [
        {"type": joint.type, "parent": joint.parent, "child": joint.child}
        for joint in joints
    ]

    prune_empty_gltf_arrays(gltf)
    particle_doc = build_particle_sidecar(unit_root, input_path, output_path, parts, joints, node_by_part, scale) if write_particles else None
    return gltf, bytes(writer.data), textures, particle_doc


def append_mesh_primitives(
    gltf: dict,
    writer: BinWriter,
    mesh_root: ET.Element,
    mesh_index: int,
    materials: list[MaterialInfo],
    id_to_index: dict[int, int],
    textures: dict[str, TextureImage],
    texture_index_by_name: dict[str, int],
    material_variant_cache: dict,
    *,
    args_label: str,
    uv_transform: str,
    scale: float = 1.0,
) -> None:
    mesh_children = child_dirs(mesh_root)
    vertices_node = mesh_children.get("Vertices")
    normals_node = mesh_children.get("Normals")
    face_groups_node = mesh_children.get("Face groups")
    if vertices_node is None or face_groups_node is None:
        raise SystemExit(f"{args_label} is missing Vertices or Face groups.")

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
        raise SystemExit(f"{args_label} object vertex list is empty.")

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
        color_material = materials[material_index] if 0 <= material_index < len(materials) else default_material()

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
                position = tuple(p * scale for p in position)
                normal_index = vertex_normals[object_index] if object_index < len(vertex_normals) else 0
                normal = face_normal if flat else vector_at(surface_normals, normal_index)
                uv = transform_uv(tex_at(tex_vertices, tex_index), uv_transform)

                positions.extend(position)
                normals.extend(normal)
                uvs.extend(uv)
                if has_vertex_colors:
                    colors.extend(vertex_color(vertex_colors, object_index, color_material))

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
                has_vertex_colors,
                textures,
                texture_index_by_name,
                material_variant_cache,
                cache_prefix=args_label,
            ),
            "extras": {
                "sourceFaceGroup": group_name,
                "sourceMaterialId": material_id,
            },
        }
        gltf["meshes"][mesh_index]["primitives"].append(primitive)


def read_compound_parts(cmpnd_node: ET.Element) -> list[CompoundPart]:
    parts: list[CompoundPart] = []
    for node in cmpnd_node:
        if node.tag != "dir":
            continue
        part_dir = node.attrib.get("name", "")
        if part_dir != "Root" and not part_dir.startswith("Part"):
            continue
        children = child_dirs(node)
        file_name = c_string(file_bytes(children.get("File name")))
        object_name = c_string(file_bytes(children.get("Object name"))) or part_dir
        index = int32(file_bytes(children.get("Index")))
        if file_name:
            parts.append(CompoundPart(part_dir, object_name, file_name, index if index is not None else -1))
    return sorted(parts, key=lambda part: (part.index < 0, part.index, natural_key(part.part_dir)))


def resolve_compound_part_units(
    unit_root: ET.Element,
    input_path: Path,
    parts: list[CompoundPart],
) -> list[tuple[CompoundPart, ET.Element, ET.Element]]:
    embedded = {
        child.attrib.get("name", "").lower(): child
        for child in unit_root
        if child.tag == "dir" and child_dir(child, "openFLAME 3D N-mesh") is not None
    }

    resolved: list[tuple[CompoundPart, ET.Element, ET.Element]] = []
    for part in parts:
        part_unit = embedded.get(part.file_name.lower())
        if part_unit is None:
            external_path = input_path.parent / f"{part.file_name}.xml"
            if external_path.exists():
                part_unit = ET.parse(external_path).getroot()
        if part_unit is None:
            continue

        mesh_root = child_dir(part_unit, "openFLAME 3D N-mesh")
        if mesh_root is not None:
            resolved.append((part, part_unit, mesh_root))
    return resolved


def read_compound_joints(cons_node: ET.Element | None) -> list[CompoundJoint]:
    if cons_node is None:
        return []
    children = child_dirs(cons_node)
    return [
        *read_fixed_like_joints(children.get("Fix"), "fixed"),
        *read_fixed_like_joints(children.get("Trans"), "translational"),
        *read_fixed_like_joints(children.get("Loose"), "loose"),
        *read_rev_like_joints(children.get("Rev"), "revolute"),
        *read_rev_like_joints(children.get("Pris"), "prismatic"),
        *read_sphere_joints(children.get("Sphere")),
    ]


def read_fixed_like_joints(node: ET.Element | None, joint_type: str) -> list[CompoundJoint]:
    data = file_bytes(node)
    if not data:
        return []
    record_size = 64 + 64 + 12 + 36
    out: list[CompoundJoint] = []
    for offset in range(0, len(data) - record_size + 1, record_size):
        out.append(
            CompoundJoint(
                joint_type,
                fixed_string(data, offset, 64),
                fixed_string(data, offset + 64, 64),
                read_vec3_bytes(data, offset + 128),
                read_mat3_bytes(data, offset + 140),
                (0.0, 0.0, 0.0),
                (0.0, 0.0, 0.0),
                (0.0, 0.0, 1.0),
            )
        )
    return out


def read_rev_like_joints(node: ET.Element | None, joint_type: str) -> list[CompoundJoint]:
    data = file_bytes(node)
    if not data:
        return []
    record_size = 64 + 64 + 12 + 12 + 36 + 12 + 4 + 4
    out: list[CompoundJoint] = []
    for offset in range(0, len(data) - record_size + 1, record_size):
        out.append(
            CompoundJoint(
                joint_type,
                fixed_string(data, offset, 64),
                fixed_string(data, offset + 64, 64),
                (0.0, 0.0, 0.0),
                read_mat3_bytes(data, offset + 152),
                read_vec3_bytes(data, offset + 128),
                read_vec3_bytes(data, offset + 140),
                read_vec3_bytes(data, offset + 188),
            )
        )
    return out


def read_sphere_joints(node: ET.Element | None) -> list[CompoundJoint]:
    data = file_bytes(node)
    if not data:
        return []
    record_size = 64 + 64 + 12 + 12 + 36 + 24
    out: list[CompoundJoint] = []
    for offset in range(0, len(data) - record_size + 1, record_size):
        out.append(
            CompoundJoint(
                "spherical",
                fixed_string(data, offset, 64),
                fixed_string(data, offset + 64, 64),
                (0.0, 0.0, 0.0),
                read_mat3_bytes(data, offset + 152),
                read_vec3_bytes(data, offset + 128),
                read_vec3_bytes(data, offset + 140),
                (0.0, 0.0, 1.0),
            )
        )
    return out


def attach_compound_nodes(gltf: dict, node_by_part: dict[str, int], joints: list[CompoundJoint], scale: float) -> None:
    root_children = gltf["nodes"][0].setdefault("children", [])
    children_by_parent: dict[str, list[CompoundJoint]] = {}
    for joint in joints:
        children_by_parent.setdefault(joint.parent, []).append(joint)

    attached: set[str] = set()

    def attach(part_name: str, parent_node_index: int) -> None:
        node_index = node_by_part.get(part_name)
        if node_index is None or part_name in attached:
            return
        attached.add(part_name)
        parent_children = gltf["nodes"][parent_node_index].setdefault("children", [])
        if node_index not in parent_children:
            parent_children.append(node_index)
        for joint in children_by_parent.get(part_name, []):
            child_index = node_by_part.get(joint.child)
            if child_index is None:
                continue
            translation, rotation = joint_local_trs(joint, scale)
            gltf["nodes"][child_index]["translation"] = list(translation)
            gltf["nodes"][child_index]["rotation"] = list(rotation)
            attach(joint.child, node_index)

    if "Root" in node_by_part:
        attach("Root", 0)

    for part_name, node_index in node_by_part.items():
        if part_name not in attached:
            root_children.append(node_index)


def build_particle_sidecar(
    unit_root: ET.Element,
    input_path: Path,
    output_path: Path,
    parts: list[CompoundPart],
    joints: list[CompoundJoint],
    node_by_part: dict[str, int],
    scale: float,
) -> dict:
    part_by_name = {part.object_name: part for part in parts}
    joints_by_child = {joint.child: joint for joint in joints}
    local_transforms = compute_part_local_transforms(parts, joints, scale)
    world_transforms = compute_part_world_transforms(parts, joints, scale)

    particles = []
    for part in parts:
        if not is_particle_part(part):
            continue
        local_translation, local_rotation = local_transforms.get(part.object_name, identity_trs())
        world_translation, world_rotation = world_transforms.get(part.object_name, identity_trs())
        joint = joints_by_child.get(part.object_name)
        parent_name = joint.parent if joint is not None else None
        parent_part = part_by_name.get(parent_name or "")
        source_path = input_path.parent / f"{part.file_name}.xml"
        particles.append(
            {
                "name": sanitize_name(part.object_name),
                "sourcePartDir": part.part_dir,
                "sourcePartIndex": part.index,
                "fileName": part.file_name,
                "sourceXml": source_path.name,
                "sourceXmlExists": source_path.exists(),
                "parentPartName": parent_name,
                "parentPartFileName": parent_part.file_name if parent_part is not None else None,
                "attachedToExportedMesh": parent_name in node_by_part if parent_name else False,
                "attachedToNodeName": sanitize_name(parent_name) if parent_name in node_by_part else None,
                "joint": particle_joint_extras(joint),
                "localTransform": trs_extras(local_translation, local_rotation),
                "worldTransform": trs_extras(world_translation, world_rotation),
                "notes": [
                    "localTransform is relative to parentPartName when attachedToExportedMesh is true.",
                    "worldTransform is composed through CMP joints at bind pose using the exporter scale.",
                ],
            }
        )

    return {
        "asset": {
            "version": 1,
            "generator": "Conquest Frontier Wars 3db XML converter",
            "source": input_path.name,
            "gltf": output_path.name,
            "unitName": unit_root.attrib.get("name", ""),
            "scale": scale,
        },
        "particles": particles,
        "extras": {
            "particleCount": len(particles),
            "exportedMeshPartNames": sorted(node_by_part.keys(), key=natural_key),
            "allCompoundParts": [
                {
                    "name": part.object_name,
                    "partDir": part.part_dir,
                    "fileName": part.file_name,
                    "index": part.index,
                    "isParticle": is_particle_part(part),
                    "isExportedMesh": part.object_name in node_by_part,
                }
                for part in parts
            ],
        },
    }


def is_particle_part(part: CompoundPart) -> bool:
    return part.file_name.lower().endswith(".pte")


def particle_joint_extras(joint: CompoundJoint | None) -> dict | None:
    if joint is None:
        return None
    return {
        "type": joint.type,
        "parent": joint.parent,
        "child": joint.child,
        "relativePosition": list(joint.rel_position),
        "relativeOrientationRows": list(joint.rel_orientation),
        "parentPoint": list(joint.parent_point),
        "childPoint": list(joint.child_point),
        "axis": list(joint.axis),
    }


def trs_extras(
    translation: tuple[float, float, float],
    rotation: tuple[float, float, float, float],
) -> dict:
    return {
        "translation": list(translation),
        "rotation": list(rotation),
        "rotationFormat": "xyzw",
    }


def identity_trs() -> tuple[tuple[float, float, float], tuple[float, float, float, float]]:
    return (0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0)


def compute_part_local_transforms(
    parts: list[CompoundPart],
    joints: list[CompoundJoint],
    scale: float,
) -> dict[str, tuple[tuple[float, float, float], tuple[float, float, float, float]]]:
    transforms = {"Root": identity_trs()}
    for joint in joints:
        transforms[joint.child] = joint_local_trs(joint, scale)
    for part in parts:
        transforms.setdefault(part.object_name, identity_trs())
    return transforms


def compute_part_world_transforms(
    parts: list[CompoundPart],
    joints: list[CompoundJoint],
    scale: float,
) -> dict[str, tuple[tuple[float, float, float], tuple[float, float, float, float]]]:
    local = compute_part_local_transforms(parts, joints, scale)
    children_by_parent: dict[str, list[str]] = {}
    for joint in joints:
        children_by_parent.setdefault(joint.parent, []).append(joint.child)

    world: dict[str, tuple[tuple[float, float, float], tuple[float, float, float, float]]] = {}

    def attach(part_name: str, parent_transform: tuple[tuple[float, float, float], tuple[float, float, float, float]]) -> None:
        if part_name in world:
            return
        local_transform = local.get(part_name, identity_trs())
        world_transform = compose_trs(parent_transform, local_transform)
        world[part_name] = world_transform
        for child in children_by_parent.get(part_name, []):
            attach(child, world_transform)

    attach("Root", identity_trs())
    for part in parts:
        if part.object_name not in world:
            world[part.object_name] = local.get(part.object_name, identity_trs())
    return world


def compose_trs(
    parent: tuple[tuple[float, float, float], tuple[float, float, float, float]],
    child: tuple[tuple[float, float, float], tuple[float, float, float, float]],
) -> tuple[tuple[float, float, float], tuple[float, float, float, float]]:
    parent_translation, parent_rotation = parent
    child_translation, child_rotation = child
    rotated_child = rotate_vector_by_quaternion(child_translation, parent_rotation)
    translation = tuple(parent_translation[i] + rotated_child[i] for i in range(3))
    rotation = quaternion_multiply(parent_rotation, child_rotation)
    return translation, rotation


def quaternion_multiply(
    a: tuple[float, float, float, float],
    b: tuple[float, float, float, float],
) -> tuple[float, float, float, float]:
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return normalize_quaternion(
        (
            aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz,
        )
    )


def rotate_vector_by_quaternion(
    vector: tuple[float, float, float],
    quat: tuple[float, float, float, float],
) -> tuple[float, float, float]:
    rotation = quaternion_to_rows(quat)
    return transform_point(rotation, vector)


def add_compound_animations(
    gltf: dict,
    writer: BinWriter,
    unit_root: ET.Element,
    node_by_part: dict[str, int],
    joints: list[CompoundJoint],
    scale: float,
) -> None:
    tracks = read_compound_animation_tracks(unit_root, joints, scale)
    if not tracks:
        return

    animations_by_name: dict[str, dict] = {}
    skipped_tracks = []
    for track in tracks:
        node_index = node_by_part.get(track.target_name)
        if node_index is None:
            skipped_tracks.append(animation_track_extras(track, "target node was not exported"))
            continue
        if not track.times:
            skipped_tracks.append(animation_track_extras(track, "channel has no keyframes"))
            continue

        animation = animations_by_name.setdefault(
            track.script_name,
            {
                "name": sanitize_name(track.script_name),
                "samplers": [],
                "channels": [],
                "extras": {"sourceTracks": []},
            },
        )
        input_accessor = add_animation_input_accessor(gltf, writer, track.times)

        if track.translations is not None:
            output_accessor = writer.add_accessor(
                gltf,
                pack_floats(value for vec in track.translations for value in vec),
                component_type=COMPONENT_FLOAT,
                type_name="VEC3",
                count=len(track.translations),
            )
            sampler_index = len(animation["samplers"])
            animation["samplers"].append({"input": input_accessor, "output": output_accessor, "interpolation": "LINEAR"})
            animation["channels"].append({"sampler": sampler_index, "target": {"node": node_index, "path": "translation"}})

        if track.rotations is not None:
            output_accessor = writer.add_accessor(
                gltf,
                pack_floats(value for quat in track.rotations for value in quat),
                component_type=COMPONENT_FLOAT,
                type_name="VEC4",
                count=len(track.rotations),
            )
            sampler_index = len(animation["samplers"])
            animation["samplers"].append({"input": input_accessor, "output": output_accessor, "interpolation": "LINEAR"})
            animation["channels"].append({"sampler": sampler_index, "target": {"node": node_index, "path": "rotation"}})

        animation["extras"]["sourceTracks"].append(animation_track_extras(track, ""))

    for animation in animations_by_name.values():
        if animation["channels"]:
            gltf["animations"].append(animation)

    if skipped_tracks:
        gltf["nodes"][0].setdefault("extras", {})["skippedCompoundAnimationTracks"] = skipped_tracks


def read_compound_animation_tracks(
    unit_root: ET.Element,
    joints: list[CompoundJoint],
    scale: float,
) -> list[CompoundAnimationTrack]:
    animation_root = child_dir(unit_root, "Animation")
    if animation_root is None:
        return []

    chnl = child_dir(animation_root, "Chnl")
    channel_nodes = list(chnl) if chnl is not None else []
    channel_library = {
        channel.attrib.get("name", ""): channel
        for channel in channel_nodes
        if channel.tag == "dir"
    }
    joint_by_child = {joint.child: joint for joint in joints}
    tracks: list[CompoundAnimationTrack] = []

    script_root = child_dir(animation_root, "Script")
    if script_root is None:
        return tracks

    for script in [child for child in script_root if child.tag == "dir"]:
        script_name = script.attrib.get("name", "Animation")
        for map_node in [child for child in script if child.tag == "dir"]:
            map_name = map_node.attrib.get("name", "")
            if not (map_name.startswith("Joint map") or map_name.startswith("Object map")):
                continue

            children = child_dirs(map_node)
            channel_node = child_dir(map_node, "Channel")
            channel_name = c_string(file_bytes(children.get("Channel name")))
            if channel_node is None and channel_name:
                channel_node = channel_library.get(channel_name)
            if channel_node is None:
                continue

            child_name = c_string(file_bytes(children.get("Child name")))
            parent_name = c_string(file_bytes(children.get("Parent name")))
            target_name = child_name or parent_name
            joint = joint_by_child.get(child_name)
            track = decode_compound_animation_channel(
                channel_node,
                script_name,
                map_name,
                target_name,
                parent_name,
                channel_name or channel_node.attrib.get("name", ""),
                joint,
                scale,
            )
            if track is not None:
                tracks.append(track)
    return tracks


def decode_compound_animation_channel(
    channel_node: ET.Element,
    script_name: str,
    map_name: str,
    target_name: str,
    parent_name: str,
    channel_name: str,
    joint: CompoundJoint | None,
    scale: float,
) -> CompoundAnimationTrack | None:
    children = child_dirs(channel_node)
    header = file_bytes(children.get("Header"))
    frames = file_bytes(children.get("Frames"))
    if not header or not frames or len(header) < 12:
        return None

    frame_count, frame_step, channel_type = struct.unpack_from("<ifi", header, 0)
    if frame_count <= 0:
        return None

    values = float_array(frames)
    explicit_times = frame_step < 0.0
    if channel_type == 1:
        stride = 2 if explicit_times else 1
        if len(values) < frame_count * stride:
            return None
        times = [values[i * stride] if explicit_times else i * frame_step for i in range(frame_count)]
        scalars = [values[i * stride + (1 if explicit_times else 0)] for i in range(frame_count)]
        if joint is not None:
            translations: list[tuple[float, float, float]] | None = None
            rotations: list[tuple[float, float, float, float]] | None = None
            if joint.type == "prismatic":
                translations = [joint_state_trs(joint, scale, scalar=value)[0] for value in scalars]
            elif joint.type == "revolute":
                poses = [joint_state_trs(joint, scale, scalar=value) for value in scalars]
                translations = [pose[0] for pose in poses]
                rotations = [pose[1] for pose in poses]
            else:
                rotations = [axis_angle_quaternion(joint.axis, value) for value in scalars]
            return CompoundAnimationTrack(
                script_name,
                map_name,
                target_name,
                parent_name,
                channel_name,
                channel_type,
                times,
                rotations=rotations,
                translations=translations,
            )
        rotations = [axis_angle_quaternion((0.0, 0.0, 1.0), value) for value in scalars]
        return CompoundAnimationTrack(script_name, map_name, target_name, parent_name, channel_name, channel_type, times, rotations=rotations)

    if channel_type == 4:
        stride = 5 if explicit_times else 4
        if len(values) < frame_count * stride:
            return None
        times = [values[i * stride] if explicit_times else i * frame_step for i in range(frame_count)]
        offset = 1 if explicit_times else 0
        rotations = [
            source_quat_to_gltf(values[i * stride + offset : i * stride + offset + 4])
            for i in range(frame_count)
        ]
        if joint is not None:
            poses = [joint_state_trs(joint, scale, rotation=quat) for quat in rotations]
            return CompoundAnimationTrack(
                script_name,
                map_name,
                target_name,
                parent_name,
                channel_name,
                channel_type,
                times,
                rotations=[pose[1] for pose in poses],
                translations=[pose[0] for pose in poses],
            )
        return CompoundAnimationTrack(script_name, map_name, target_name, parent_name, channel_name, channel_type, times, rotations=rotations)

    if channel_type == 6:
        stride = 8 if explicit_times else 7
        if len(values) < frame_count * stride:
            return None
        times = [values[i * stride] if explicit_times else i * frame_step for i in range(frame_count)]
        offset = 1 if explicit_times else 0
        raw_translations = [
            (
                values[i * stride + offset],
                values[i * stride + offset + 1],
                values[i * stride + offset + 2],
            )
            for i in range(frame_count)
        ]
        rotations = [
            source_quat_to_gltf(values[i * stride + offset + 3 : i * stride + offset + 7])
            for i in range(frame_count)
        ]
        if joint is not None:
            poses = [
                joint_state_trs(joint, scale, translation=translation, rotation=rotation)
                for translation, rotation in zip(raw_translations, rotations)
            ]
            return CompoundAnimationTrack(
                script_name,
                map_name,
                target_name,
                parent_name,
                channel_name,
                channel_type,
                times,
                rotations=[pose[1] for pose in poses],
                translations=[pose[0] for pose in poses],
            )
        translations = [tuple(value * scale for value in translation) for translation in raw_translations]
        return CompoundAnimationTrack(
            script_name,
            map_name,
            target_name,
            parent_name,
            channel_name,
            channel_type,
            times,
            rotations=rotations,
            translations=translations,
        )

    return None


def add_animation_input_accessor(gltf: dict, writer: BinWriter, times: list[float]) -> int:
    return writer.add_accessor(
        gltf,
        pack_floats(times),
        component_type=COMPONENT_FLOAT,
        type_name="SCALAR",
        count=len(times),
        min_value=[min(times)],
        max_value=[max(times)],
    )


def animation_track_extras(track: CompoundAnimationTrack, reason: str) -> dict:
    out = {
        "script": track.script_name,
        "map": track.map_name,
        "targetName": track.target_name,
        "parentName": track.parent_name,
        "channelName": track.channel_name,
        "channelType": track.channel_type,
        "keyframes": len(track.times),
    }
    if reason:
        out["reason"] = reason
    return out


def source_quat_to_gltf(values: list[float]) -> tuple[float, float, float, float]:
    if len(values) < 4:
        return (0.0, 0.0, 0.0, 1.0)
    w, x, y, z = values[:4]
    return normalize_quaternion((x, y, z, w))


def axis_angle_quaternion(axis: tuple[float, float, float], angle: float) -> tuple[float, float, float, float]:
    half = angle * 0.5
    sin_half = math.sin(half)
    x, y, z = axis
    return normalize_quaternion((x * sin_half, y * sin_half, z * sin_half, math.cos(half)))


def normalize_quaternion(quat: tuple[float, float, float, float]) -> tuple[float, float, float, float]:
    length = math.sqrt(sum(value * value for value in quat))
    if length <= 0.0:
        return (0.0, 0.0, 0.0, 1.0)
    return tuple(value / length for value in quat)  # type: ignore[return-value]


def joint_state_trs(
    joint: CompoundJoint,
    scale: float,
    *,
    scalar: float = 0.0,
    translation: tuple[float, float, float] | None = None,
    rotation: tuple[float, float, float, float] | None = None,
) -> tuple[tuple[float, float, float], tuple[float, float, float, float]]:
    state_translation = translation if translation is not None else (0.0, 0.0, 0.0)
    state_rotation = rotation if rotation is not None else (0.0, 0.0, 0.0, 1.0)
    rel_orientation = joint.rel_orientation

    if joint.type == "revolute":
        local_rotation = mat3_multiply(quaternion_to_rows(axis_angle_quaternion(joint.axis, scalar)), rel_orientation)
        local_translation = tuple(
            joint.parent_point[i] - transform_point(local_rotation, joint.child_point)[i]
            for i in range(3)
        )
    elif joint.type == "prismatic":
        local_rotation = rel_orientation
        child_point = transform_point(local_rotation, joint.child_point)
        local_translation = tuple(
            joint.parent_point[i] + joint.axis[i] * scalar - child_point[i]
            for i in range(3)
        )
    elif joint.type == "spherical":
        local_rotation = mat3_multiply(quaternion_to_rows(state_rotation), rel_orientation)
        local_translation = tuple(
            joint.parent_point[i] - transform_point(local_rotation, joint.child_point)[i]
            for i in range(3)
        )
    elif joint.type == "translational":
        local_rotation = rel_orientation
        local_translation = tuple(joint.rel_position[i] + state_translation[i] for i in range(3))
    elif joint.type == "loose":
        local_rotation = mat3_multiply(quaternion_to_rows(state_rotation), rel_orientation)
        local_translation = tuple(joint.rel_position[i] + state_translation[i] for i in range(3))
    else:
        local_rotation = rel_orientation
        local_translation = joint.rel_position

    scaled_translation = tuple(value * scale for value in local_translation)
    return scaled_translation, rotation_rows_to_quaternion(local_rotation)


def joint_local_trs(joint: CompoundJoint, scale: float) -> tuple[tuple[float, float, float], tuple[float, float, float, float]]:
    return joint_state_trs(joint, scale)


def rotation_rows_to_quaternion(rotation: tuple[float, ...]) -> tuple[float, float, float, float]:
    r00, r01, r02, r10, r11, r12, r20, r21, r22 = rotation[:9]
    trace = r00 + r11 + r22
    if trace > 0.0:
        s = math.sqrt(trace + 1.0) * 2.0
        w = 0.25 * s
        x = (r21 - r12) / s
        y = (r02 - r20) / s
        z = (r10 - r01) / s
    elif r00 > r11 and r00 > r22:
        s = math.sqrt(1.0 + r00 - r11 - r22) * 2.0
        w = (r21 - r12) / s
        x = 0.25 * s
        y = (r01 + r10) / s
        z = (r02 + r20) / s
    elif r11 > r22:
        s = math.sqrt(1.0 + r11 - r00 - r22) * 2.0
        w = (r02 - r20) / s
        x = (r01 + r10) / s
        y = 0.25 * s
        z = (r12 + r21) / s
    else:
        s = math.sqrt(1.0 + r22 - r00 - r11) * 2.0
        w = (r10 - r01) / s
        x = (r02 + r20) / s
        y = (r12 + r21) / s
        z = 0.25 * s
    return normalize_quaternion((x, y, z, w))


def quaternion_to_rows(quat: tuple[float, float, float, float]) -> tuple[float, ...]:
    x, y, z, w = normalize_quaternion(quat)
    xx = x * x
    yy = y * y
    zz = z * z
    xy = x * y
    xz = x * z
    yz = y * z
    wx = w * x
    wy = w * y
    wz = w * z
    return (
        1.0 - 2.0 * (yy + zz),
        2.0 * (xy - wz),
        2.0 * (xz + wy),
        2.0 * (xy + wz),
        1.0 - 2.0 * (xx + zz),
        2.0 * (yz - wx),
        2.0 * (xz - wy),
        2.0 * (yz + wx),
        1.0 - 2.0 * (xx + yy),
    )


def mat3_multiply(a: tuple[float, ...], b: tuple[float, ...]) -> tuple[float, ...]:
    return (
        a[0] * b[0] + a[1] * b[3] + a[2] * b[6],
        a[0] * b[1] + a[1] * b[4] + a[2] * b[7],
        a[0] * b[2] + a[1] * b[5] + a[2] * b[8],
        a[3] * b[0] + a[4] * b[3] + a[5] * b[6],
        a[3] * b[1] + a[4] * b[4] + a[5] * b[7],
        a[3] * b[2] + a[4] * b[5] + a[5] * b[8],
        a[6] * b[0] + a[7] * b[3] + a[8] * b[6],
        a[6] * b[1] + a[7] * b[4] + a[8] * b[7],
        a[6] * b[2] + a[7] * b[5] + a[8] * b[8],
    )


def transform_point(rotation: tuple[float, ...], point: tuple[float, float, float]) -> tuple[float, float, float]:
    e00, e01, e02, e10, e11, e12, e20, e21, e22 = rotation[:9]
    x, y, z = point
    return (
        e00 * x + e01 * y + e02 * z,
        e10 * x + e11 * y + e12 * z,
        e20 * x + e21 * y + e22 * z,
    )


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


def texture_info(
    gltf: dict,
    texture_index_by_name: dict[str, int],
    texture_name: str | None,
    texture_flags: int,
) -> dict | None:
    texture_index = texture_lookup_with_sampler(gltf, texture_index_by_name, texture_name, texture_flags)
    if texture_index is None:
        return None
    return {"index": texture_index}


def texture_lookup_with_sampler(
    gltf: dict,
    texture_index_by_name: dict[str, int],
    texture_name: str | None,
    texture_flags: int,
) -> int | None:
    base_index = texture_lookup(texture_index_by_name, texture_name)
    if base_index is None or not texture_name:
        return None

    variant_key = f"{Path(texture_name).stem.lower()}#sampler:{texture_flags & 0xff}"
    variant_index = texture_index_by_name.get(variant_key)
    if variant_index is not None:
        return variant_index

    sampler = sampler_from_texture_flags(texture_flags)
    base_texture = gltf["textures"][base_index]
    base_sampler = gltf.get("samplers", [])[base_texture.get("sampler", -1)] if base_texture.get("sampler", -1) >= 0 else None
    if base_sampler == sampler:
        texture_index_by_name[variant_key] = base_index
        return base_index

    sampler_index = len(gltf["samplers"])
    gltf["samplers"].append(sampler)
    new_texture = {
        "sampler": sampler_index,
        "source": base_texture["source"],
        "name": f"{base_texture.get('name', Path(texture_name).stem)}_flags_{texture_flags & 0xff:02x}",
        "extras": {
            "sourceTextureName": texture_name,
            "sourceTextureFlags": texture_flags,
            "sourceTextureAddress": texture_address_extras(texture_flags),
        },
    }
    new_index = len(gltf["textures"])
    gltf["textures"].append(new_texture)
    texture_index_by_name[variant_key] = new_index
    return new_index


def material_variant(
    gltf: dict,
    materials: list[MaterialInfo],
    material_index: int,
    double_sided: bool,
    use_vertex_colors: bool,
    textures: dict[str, TextureImage],
    texture_index_by_name: dict[str, int],
    cache: dict[tuple[int, bool], int],
    cache_prefix: str = "",
) -> int:
    key = (cache_prefix, material_index, double_sided, use_vertex_colors)
    if key in cache:
        return cache[key]

    mat = materials[material_index] if 0 <= material_index < len(materials) else default_material()
    alpha = max(0.0, min(1.0, mat.transparency))
    color_factor = (1.0, 1.0, 1.0) if use_vertex_colors else material_tint_color(mat)
    pbr = {
        "baseColorFactor": [color_factor[0], color_factor[1], color_factor[2], alpha],
        "metallicFactor": 0.0,
        "roughnessFactor": shininess_to_roughness(mat.shininess),
    }

    texture_name, texture_flags = material_base_color_texture(mat)
    texture_index = texture_lookup_with_sampler(gltf, texture_index_by_name, texture_name, texture_flags)
    texture_has_alpha = texture_name_has_alpha(textures, texture_name)
    if texture_index is not None:
        pbr["baseColorTexture"] = {"index": texture_index}

    emissive_texture = texture_info(gltf, texture_index_by_name, mat.emission_texture_name, mat.emission_texture_flags)

    out = {
        "name": mat.name + ("_doubleSided" if double_sided else ""),
        "pbrMetallicRoughness": pbr,
        "doubleSided": double_sided,
        "extras": {
            "sourceMaterialIdentifier": mat.identifier,
            "sourceSpecular": list(mat.specular),
            "sourceDiffuseTextureName": mat.diffuse_texture_name,
            "sourceDiffuseTextureFlags": mat.diffuse_texture_flags,
            "sourceSecondDiffuseTextureName": mat.second_diffuse_texture_name,
            "sourceSecondDiffuseTextureFlags": mat.second_diffuse_texture_flags,
            "sourceEmissionTextureName": mat.emission_texture_name,
            "sourceEmissionTextureFlags": mat.emission_texture_flags,
            "sourceEmissionTextureBlend": mat.emission_texture_blend,
            "cfwMaterial": legacy_material_extras(mat, texture_name, texture_flags, use_vertex_colors),
        },
    }
    if emissive_texture is not None:
        out["emissiveTexture"] = emissive_texture
        out["emissiveFactor"] = [mat.emission_texture_blend] * 3
    elif any(mat.emission):
        out["emissiveFactor"] = list(mat.emission)
    if texture_index is not None:
        out.setdefault("extensions", {})["KHR_materials_unlit"] = {}
        add_gltf_extension_used(gltf, "KHR_materials_unlit")
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
        bump_node = children.get("Bump")
        shininess = tuple(float_array(file_bytes(child_file(child_dirs(children.get("Shininess")), "Constant"))))
        transparency_values = float_array(file_bytes(child_file(child_dirs(children.get("Transparency")), "Constant")))
        diffuse_texture_name = map_name(diffuse_node)
        texture_flags = int32(file_bytes(child_file(child_dirs(child_dirs(diffuse_node).get("Map")), "Flags"))) or 0
        second_diffuse_texture_name = map_name(bump_node)
        second_diffuse_texture_flags = int32(file_bytes(child_file(child_dirs(child_dirs(bump_node).get("Map")), "Flags"))) or 0
        emission_texture_name = map_name(emission_node)
        emission_texture_flags = int32(file_bytes(child_file(child_dirs(child_dirs(emission_node).get("Map")), "Flags"))) or 0
        emission_texture_blend_values = float_array(file_bytes(child_file(child_dirs(child_dirs(emission_node).get("Map")), "Blend")))
        emission_texture_blend = max(0.0, min(1.0, emission_texture_blend_values[0] if emission_texture_blend_values else 1.0))
        identifier_value = int32(file_bytes(children.get("Material identifier")))
        identifier = identifier_value if identifier_value is not None else len(materials)
        material = MaterialInfo(
            name=mat_node.attrib.get("name", f"Material_{len(materials)}"),
            diffuse=read_color(child_file(child_dirs(diffuse_node), "Constant"), (1.0, 1.0, 1.0)),
            emission=read_color(child_file(child_dirs(emission_node), "Constant"), (0.0, 0.0, 0.0)),
            specular=read_color(child_file(child_dirs(children.get("Specular")), "Constant"), (0.08, 0.08, 0.08)),
            transparency=transparency_values[0] if transparency_values else 1.0,
            shininess=shininess,
            diffuse_texture_name=diffuse_texture_name,
            diffuse_texture_flags=texture_flags,
            second_diffuse_texture_name=second_diffuse_texture_name,
            second_diffuse_texture_flags=second_diffuse_texture_flags,
            emission_texture_name=emission_texture_name,
            emission_texture_flags=emission_texture_flags,
            emission_texture_blend=emission_texture_blend,
            identifier=identifier,
        )
        id_to_index[identifier] = len(materials)
        materials.append(material)

    if not materials:
        materials.append(default_material())
        id_to_index[0] = 0
    return materials, id_to_index


def load_textures(mesh_root: ET.Element) -> dict[str, TextureImage]:
    texture_library = mesh_root if mesh_root.attrib.get("name") == "Texture library" else child_dir(mesh_root, "Texture library")
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
    add_hardpoint_nodes_to_parent(gltf, unit_root, 0)


def add_hardpoint_nodes_to_parent(gltf: dict, unit_root: ET.Element, parent_node_index: int) -> None:
    hardpoints = child_dir(unit_root, "Hardpoints")
    fixed = child_dir(hardpoints, "Fixed") if hardpoints is not None else None
    if fixed is None:
        return

    parent_children = gltf["nodes"][parent_node_index].setdefault("children", [])
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


def c_string(data: bytes | None) -> str:
    if not data:
        return ""
    return data.split(b"\x00", 1)[0].decode("ascii", errors="replace")


def fixed_string(data: bytes, offset: int, size: int) -> str:
    return c_string(data[offset : offset + size])


def read_vec3_bytes(data: bytes, offset: int) -> tuple[float, float, float]:
    if offset + 12 > len(data):
        return (0.0, 0.0, 0.0)
    return struct.unpack_from("<fff", data, offset)


def read_mat3_bytes(data: bytes, offset: int) -> tuple[float, ...]:
    if offset + 36 > len(data):
        return (1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0)
    return struct.unpack_from("<fffffffff", data, offset)


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


def vertex_color(vertex_colors: bytes, object_index: int, material: MaterialInfo) -> list[int]:
    base = tuple(max(0.0, min(1.0, material.diffuse[i] + material.emission[i])) for i in range(3))
    offset = object_index * 3
    if offset + 2 < len(vertex_colors):
        return [
            round(base[0] * vertex_colors[offset]),
            round(base[1] * vertex_colors[offset + 1]),
            round(base[2] * vertex_colors[offset + 2]),
        ]
    return [round(base[0] * 255), round(base[1] * 255), round(base[2] * 255)]


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
    return MaterialInfo(
        "Default",
        (1.0, 1.0, 1.0),
        (0.0, 0.0, 0.0),
        (0.08, 0.08, 0.08),
        1.0,
        (0.15,),
        None,
        0,
        None,
        0,
        None,
        0,
        1.0,
        0,
    )


def material_base_color_texture_name(mat: MaterialInfo) -> str | None:
    return material_base_color_texture(mat)[0]


def material_base_color_texture(mat: MaterialInfo) -> tuple[str | None, int]:
    if mat.emission_texture_name and is_environment_texture_name(mat.diffuse_texture_name):
        return mat.emission_texture_name, mat.emission_texture_flags
    return mat.diffuse_texture_name, mat.diffuse_texture_flags


def material_tint_color(mat: MaterialInfo) -> tuple[float, float, float]:
    return tuple(
        max(0.0, min(1.0, mat.diffuse[i] + mat.emission[i]))
        for i in range(3)
    )


def is_environment_texture_name(name: str | None) -> bool:
    if not name:
        return False
    stem = Path(name).stem.lower()
    return stem.startswith("environ")


def sampler_from_texture_flags(texture_flags: int) -> dict:
    return {
        "magFilter": 9728,
        "minFilter": 9987,
        "wrapS": gltf_wrap_mode(texture_flags & 0x03),
        "wrapT": gltf_wrap_mode((texture_flags & 0x0c) >> 2),
    }


def gltf_wrap_mode(address_mode: int) -> int:
    if address_mode == 1:
        return 33648
    if address_mode in (2, 3):
        return 33071
    return 10497


def texture_address_extras(texture_flags: int) -> dict:
    wrap_mode = (texture_flags & 0xf0) >> 4
    return {
        "u": texture_flags & 0x03,
        "v": (texture_flags & 0x0c) >> 2,
        "coordinateSet": 1 if wrap_mode == 5 else 0,
        "wrapMode": wrap_mode,
    }


def legacy_material_extras(
    mat: MaterialInfo,
    exported_base_texture_name: str | None,
    exported_base_texture_flags: int,
    use_vertex_colors: bool,
) -> dict:
    return {
        "diffuseTexture": mat.diffuse_texture_name,
        "diffuseFlags": mat.diffuse_texture_flags,
        "diffuseAddress": texture_address_extras(mat.diffuse_texture_flags),
        "secondDiffuseTexture": mat.second_diffuse_texture_name,
        "secondDiffuseFlags": mat.second_diffuse_texture_flags,
        "secondDiffuseAddress": texture_address_extras(mat.second_diffuse_texture_flags),
        "emissionTexture": mat.emission_texture_name,
        "emissionFlags": mat.emission_texture_flags,
        "emissionAddress": texture_address_extras(mat.emission_texture_flags),
        "emissionBlend": mat.emission_texture_blend,
        "diffuseConstant": list(mat.diffuse),
        "emissionConstant": list(mat.emission),
        "specular": list(mat.specular),
        "transparency": mat.transparency,
        "shininess": list(mat.shininess),
        "exportedBaseColorTexture": exported_base_texture_name,
        "exportedBaseColorFlags": exported_base_texture_flags,
        "usesVertexColors": use_vertex_colors,
        "usesEnvironmentDiffuseAsChrome": is_environment_texture_name(mat.diffuse_texture_name),
        "recommendedGodotShader": "cfw_legacy_lit",
    }


def texture_lookup(texture_index_by_name: dict[str, int], name: str | None) -> int | None:
    if not name:
        return None
    exact = texture_index_by_name.get(name.lower())
    if exact is not None:
        return exact
    return texture_index_by_name.get(Path(name).stem.lower())


def add_gltf_extension_used(gltf: dict, extension_name: str) -> None:
    extensions = gltf.setdefault("extensionsUsed", [])
    if extension_name not in extensions:
        extensions.append(extension_name)


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
