# Conquest Frontier Wars Mesh Format Notes

These files are not a single modern model format. They are UTF containers that act like a small virtual filesystem. Each named directory/file inside the container stores engine-specific records, most binary, sometimes represented as base64 in XML dumps.

## Container Shape

The binary `.3db`, `.cmp`, and `.shield` files use a UTF container. In the XML representation:

- `<unit name="...">` is the root object.
- `<dir name="...">` is a directory.
- `<file name="...">base64...</file>` is a binary file payload.

The browser parser turns this into a tree:

```js
{
  "\\": {
    name: "file.cmp",
    children: {
      "Cmpnd": { children: ... },
      "Texture library": { children: ... }
    }
  }
}
```

All numeric payloads seen so far are little-endian.

## `.3db` PolyMesh

Most normal mesh files contain:

```text
openFLAME 3D N-mesh/
  Vertices/
  Normals/
  Face groups/
  Material library/
  Texture library/
  Sphere/
  Edges/
```

Important render fields:

- `Vertices/Object vertex list`: float triplets, object-space vertex positions.
- `Vertices/Vertex batch list`: int indices into object vertices.
- `Vertices/Texture vertex list`: float pairs, UV coordinates.
- `Vertices/Texture batch list`: int indices into texture vertices.
- `Vertices/Texture batch list2`: optional second UV channel.
- `Vertices/Vertex normal`: int indices into surface normal list.
- `Normals/Surface normal list`: float triplets.
- `Face groups/GroupN/Face vertex chain`: int triplets indexing the batch lists.
- `Face groups/GroupN/Face normal`: per-face normal index.
- `Face groups/GroupN/Face property`: flags:
  - `0x01` two-sided
  - `0x02` flat shaded
  - `0x04` smooth shaded
  - `0x08` hidden
- `Face groups/GroupN/Material`: material id.

The original renderer expands face groups through the batch lists, chooses face normals for flat-shaded faces and vertex normals for smooth faces, applies material/texture passes, and renders each face group.

## Materials And Textures

`Material library` contains named material directories. Material ids are stored in `Material identifier`; face groups refer to those ids.

Common material fields:

- `Ambient/Constant`: 3 floats.
- `Diffuse/Constant`: 3 floats.
- `Diffuse/Map/Name`: texture name string.
- `Diffuse/Map/Flags`: texture address/wrap flags.
- `Emission/Constant` and `Emission/Map`: emissive color/map.
- `Specular/Constant`: specular color.
- `Shininess/Constant`: usually width/strength floats.
- `Transparency/Constant`: one float, if present.

## Texture Formats

The authoritative names are in `TextureLibrary.cpp`:

```text
Image X size
Image Y size
Palette color count
Alpha 8 bit
Image indices
Image colors
Palette RGB 888
Palette 8 bit
True RGB 565
MIP*
Texture library
Animation library
Texture count
Frame count
Frame rects
FPS
```

The loader supports several generations of texture layout.

### New Normal Texture Layout

Common on `.3db` and `.cmp` assets:

```text
Texture library/
  textureName/
    Image X size
    Image Y size
    Palette 8 bit/
      Palette RGB 888
      MIP0/
        Image indices
      MIP1/
        Image indices
```

or:

```text
Texture library/
  textureName/
    Image X size
    Image Y size
    True RGB 565/
      MIP0/
        Image colors
        Alpha 8 bit
```

`Palette 8 bit` is indexed color. `Image indices` are one byte per pixel and `Palette RGB 888` is 256 RGB triples unless `Palette color count` overrides it.

`True RGB 565` stores little-endian 16-bit pixels:

```text
rrrrrggg gggbbbbb
```

An optional `Alpha 8 bit` payload is one byte per pixel.

### Old MIP-Local Layout

Particle files (`.pte`) commonly put the format inside `MIP0`:

```text
Texture library/
  spine2-64.bmp/
    MIP0/
      Image X size
      Image Y size
      Palette 8 Bit/
        Image indices
        Palette RGB 888
        Palette color count
```

Note the capitalization: `Palette 8 Bit`, not `Palette 8 bit`. The original engine is effectively case-tolerant through the filesystem; browser importers should be too.

### Generic Persisted PixelFormat Layout

`PixelFormat::persist()` writes names like:

```text
Format_PAL8_3__8_8_8
Format_TRUE_3__8_8_8
Format_TRUE_4__8_8_8_8
Format_TRUE_2__8_8
```

Observed in `bh_rings.3db.xml`:

```text
Texture library/
  BH_ringsBH_rings/
    Image X size
    Image Y size
    Format_TRUE_3__8_8_8/
      MIP0/
        Image colors
        Alpha 8 bit
```

The `Format_TRUE_N__...` suffix stores component bit widths. For example, `Format_TRUE_3__8_8_8` is 24-bit true color. `Format_PAL8...` is indexed and uses `Image indices` plus `Palette RGB 888`.

### Very Old 1.6 TGA-In-MIP Layout

`TextureLibrary::load_texture_normal_1_6()` supports a legacy layout where `MIP0`, `MIP1`, etc. are TGA file blobs rather than directories. The code parses an 18-byte TGA header and supports indexed, RGB, greyscale, 16/24/32-bit, and RLE-flagged type values, though the viewer does not currently need this for the sampled model files.

### Animated Textures

`Animation library` entries are compound texture objects:

```text
Animation library/
  animName/
    Texture count
    Frame count
    Frame rects
    FPS
```

The actual child textures are loaded as:

```text
animName_0
animName_1
...
```

`Frame rects` are `ITL_TEXTUREFRAME` records:

```text
uint32 texture_id_idx
float u0, v0, u1, v1
```

The runtime can loop, ping-pong, or play once through `ITL_PLAYCOMMAND`.

### External References

The texture loader also checks for a `Filename` field. That path is an external texture/video reference handled by `load_texture_extref*`. This is not represented in the current static viewer.

These are game-era texture payloads, not browser/native-engine-ready images. They need conversion to RGBA images before rendering in a modern engine.

## `.cmp` Compound Models

A `.cmp` is not one mesh. It is a compound model containing multiple embedded `.3db` parts plus connection data.

Typical structure:

```text
Cmpnd/
  Root/
    File name
    Object name
    Index
  Part_backleg/
  Part_left front/
  Part_rightfront/
  Cons/
    Rev
    Pris
    Sphere
Texture library/
embeddedPart.3db/
  openFLAME 3D N-mesh/
```

Part entries map object names to embedded mesh filenames. `Cmpnd/Cons` stores joint records that assemble the parts.

Relevant persisted structs are in `PERSISTCOMPOUND.H`:

- `Fix`: parent, child, relative position, relative orientation.
- `Rev`: parent, child, parent point, child point, relative orientation, axis, min/max.
- `Pris`: same binary layout as `Rev`.
- `Cyl`: revolute plus prismatic data.
- `Sphere`: parent/child anchor points, relative orientation, angular limits.

Default placement comes from `EngineInstance::update_joint`:

```text
childR = parentR * rel_orientation
childT = parentT + parentR * parent_point - childR * child_point
```

For revolute/prismatic/cylindrical joints, animation state modifies this. With no animation, the default state is zero, so the formula above is enough to place static parts.

## `.shield` Files

The shield sample is not `openFLAME 3D N-mesh`. It has old top-level blobs:

```text
Faces
Vertices
```

Observed layout:

- `Vertices`: `uint16 count`, then 24-byte records:
  - position: 3 floats
  - normal: 3 floats
- `Faces`: `uint16 count`, then 20-byte records:
  - normal/plane data appears first
  - indices are three `uint16` values at byte offsets `12`, `14`, `16`

This is a simpler special-purpose mesh, useful for transparent shield geometry.

## Difficulty Of Representing This Format

Static rendering is manageable:

- decode UTF
- extract arrays
- convert texture payloads to RGBA
- build geometry from face groups
- place compound parts with joint defaults

Full engine-equivalent representation is much harder:

- compound joints carry animation degrees of freedom
- animation channels must drive joint state vectors
- old material behavior uses multi-pass Direct3D-era render states
- UV animation and second texture channels exist
- LOD exists in `Mesh.cpp`, but was intentionally skipped for the viewer
- physics, hardpoints, rigid bodies, and gameplay metadata are mixed into the same container family

## Migration Recommendation

Do not use this format as the runtime asset format for a new engine.

Use it as an import/source format only. Convert it once into a modern engine-friendly format such as:

- glTF/GLB for mesh hierarchy, materials, textures, and transforms.
- PNG/WebP/KTX2 for converted texture payloads.
- JSON sidecars for hardpoints, gameplay sockets, joint metadata, or source-specific flags.

For a new engine, the practical pipeline should be:

1. Read UTF containers.
2. Extract `.3db`, `.cmp`, `.shield`.
3. Decode materials and textures.
4. Assemble compound transforms from `Cmpnd/Cons`.
5. Bake static geometry and default part transforms into glTF/GLB.
6. Preserve hardpoints/joints as named nodes or JSON metadata.
7. Drop legacy LOD/Direct3D render-pass behavior unless it is visually required.

This keeps the original data recoverable while avoiding a dead, engine-specific runtime dependency.
