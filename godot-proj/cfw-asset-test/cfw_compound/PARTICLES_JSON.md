# CFW Particle Sidecar JSON

The converter can write a companion file named `<model>.particles.json` for CMP files that reference `.pte` particle parts. Use this file to recreate the particle emitters in Godot after the mesh GLTF is imported.

Example:

```text
assets/trefinery/trefinery_cmp.gltf
assets/trefinery/trefinery_cmp.particles.json
```

## Top Level

```json
{
  "asset": {},
  "particles": [],
  "extras": {}
}
```

`asset` describes the source CMP and generated GLTF:

- `source`: original `.cmp.xml`
- `gltf`: matching GLTF filename
- `unitName`: source unit name
- `scale`: coordinate scale used by the exporter

`particles` is the list of `.pte` attachments to reconstruct.

`extras.exportedMeshPartNames` lists the mesh parts that became GLTF nodes.

## Particle Entry

Each item in `particles` describes one particle attachment:

```json
{
  "name": "smoke1.pte",
  "fileName": "smoke.pte",
  "sourceXml": "smoke.pte.xml",
  "sourceXmlExists": true,
  "parentPartName": "Root",
  "attachedToExportedMesh": true,
  "attachedToNodeName": "Root",
  "localTransform": {},
  "worldTransform": {},
  "joint": {}
}
```

Important fields:

- `name`: unique particle part name inside the CMP. Use this as the Godot node name.
- `fileName`: particle definition filename from the original CMP, usually ending in `.pte`.
- `sourceXml`: expected XML dump for the particle definition.
- `sourceXmlExists`: whether that `.pte.xml` file exists next to the source XML dump.
- `parentPartName`: compound part this particle was attached to.
- `parentPartFileName`: source mesh file of the parent part, when known.
- `attachedToExportedMesh`: true when the parent part exists as a GLTF node.
- `attachedToNodeName`: Godot/imported GLTF node name to attach under.

## Which Transform To Use

Prefer `localTransform` when `attachedToExportedMesh` is true.

Create the particle node as a child of the imported mesh node named by `attachedToNodeName`, then apply:

```gdscript
particle_node.position = Vector3(t.x, t.y, t.z)
particle_node.quaternion = Quaternion(r.x, r.y, r.z, r.w)
```

where `t` is `localTransform.translation` and `r` is `localTransform.rotation`.

Use `worldTransform` only when:

- the parent mesh node was not exported,
- you are spawning particles outside the imported GLTF hierarchy,
- or you need a debug marker in model-local/world bind-pose space.

`worldTransform` is composed through the CMP joint tree at bind pose.

## Coordinate Scale

The sidecar transform values use the same coordinate scale as the generated GLTF.

If you instance the whole model in Godot and scale the model root, attach particles under the imported mesh nodes and use `localTransform` directly. The parent model scale will affect the particle nodes automatically.

If you place particles outside the model hierarchy, apply the same model/root transform manually:

```gdscript
particle_node.global_transform = model_root.global_transform * particle_local_transform
```

## Rotation Format

Rotations are stored as glTF/Godot quaternions in `xyzw` order:

```json
"rotation": [x, y, z, w],
"rotationFormat": "xyzw"
```

In Godot:

```gdscript
var q := Quaternion(x, y, z, w)
```

## Suggested Godot Reconstruction

```gdscript
func add_particles(model_root: Node3D, particle_doc: Dictionary) -> void:
	for p in particle_doc.get("particles", []):
		if not p.get("attachedToExportedMesh", false):
			continue

		var parent_name: String = p.get("attachedToNodeName", "")
		var parent := model_root.find_child(parent_name, true, false) as Node3D
		if parent == null:
			continue

		var emitter := GPUParticles3D.new()
		emitter.name = p.get("name", "particle")

		var local := p["localTransform"]
		var t: Array = local["translation"]
		var r: Array = local["rotation"]
		emitter.position = Vector3(t[0], t[1], t[2])
		emitter.quaternion = Quaternion(r[0], r[1], r[2], r[3])

		# Use p["fileName"] or p["sourceXml"] to choose/build the particle material/process later.
		parent.add_child(emitter)
```

## Joint Data

`joint` is included for debugging and future reconstruction:

- `type`: CMP joint type used for the attachment
- `parent` / `child`: original CMP part names
- `relativePosition`: source relative position before export scaling
- `relativeOrientationRows`: source 3x3 orientation rows
- `parentPoint`, `childPoint`, `axis`: extra joint data used by revolute/prismatic/spherical joints

For normal Godot placement, use `localTransform` or `worldTransform`; do not recompute from `joint` unless debugging the exporter.
