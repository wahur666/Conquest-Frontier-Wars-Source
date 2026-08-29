@tool
extends Node3D
## Assembles a compound model with its particle emitters from a .particles.json sidecar.
##
## Attach this script to a Node3D in the scene, set model_scene_file and sidecar_file
## in the inspector, then click Rebuild Scene to populate the hierarchy.

@export_file("*.gltf", "*.glb", "*.tscn", "*.scn") var model_scene_file := ""
@export_file("*.json") var sidecar_file := ""
@export var particle_player_scene: PackedScene = preload("res://cfw_unified_particle/CFWUnifiedParticlePlayer.tscn")

@export var rebuild_scene := false:
	set(value):
		rebuild_scene = false
		if value:
			rebuild()
			notify_property_list_changed()

@export var clear_scene := false:
	set(value):
		clear_scene = false
		if value:
			_clear_generated()
			notify_property_list_changed()

func rebuild() -> void:
	_clear_generated()

	if model_scene_file.is_empty():
		push_warning("CFWCompoundSceneBuilder: model_scene_file is not set.")
		return

	var packed := ResourceLoader.load(model_scene_file) as PackedScene
	if packed == null:
		push_warning("CFWCompoundSceneBuilder: could not load model scene: %s" % model_scene_file)
		return

	var model := packed.instantiate() as Node3D
	if model == null:
		push_warning("CFWCompoundSceneBuilder: model scene root is not Node3D: %s" % model_scene_file)
		return

	model.name = "Model"
	model.set_meta("cfw_generated", true)
	add_child(model)
	_set_generated_owner(model)
	_make_editable_instance(model)

	if not sidecar_file.is_empty():
		_attach_particles(model)


func _attach_particles(model: Node3D) -> void:
	if particle_player_scene == null:
		push_warning("CFWCompoundSceneBuilder: particle_player_scene is not set.")
		return

	var data := _read_sidecar()
	if data.is_empty():
		return

	for entry in data.get("particles", []):
		if not entry is Dictionary:
			continue

		var attached: bool = entry.get("attachedToExportedMesh", false)
		var transform_key := "localTransform" if attached else "worldTransform"

		var parent: Node3D
		if attached:
			var node_name: String = entry.get("attachedToNodeName", "")
			parent = _find_node_by_name(model, node_name) as Node3D
			if parent == null:
				push_warning(
					"CFWCompoundSceneBuilder: attachment node '%s' not found, falling back to model root." % node_name
				)
				parent = model
		else:
			parent = self

		var emitter := particle_player_scene.instantiate() as Node3D
		if emitter == null:
			continue

		emitter.name = entry.get("name", "particle")
		emitter.set_meta("cfw_generated", true)

		var lt: Dictionary = entry.get(transform_key, {})
		var t: Array = lt.get("translation", [0.0, 0.0, 0.0])
		var r: Array = lt.get("rotation", [0.0, 0.0, 0.0, 1.0])
		emitter.position = Vector3(float(t[0]), float(t[1]), float(t[2]))
		emitter.quaternion = Quaternion(float(r[0]), float(r[1]), float(r[2]), float(r[3]))

		var xml_path := _resolve_xml_path(entry.get("sourceXml", ""))
		if not xml_path.is_empty():
			emitter.set("unified_xml_file", xml_path)

		parent.add_child(emitter)
		_set_generated_owner(emitter)

		if emitter.has_method("reload"):
			emitter.call("reload")


## Looks for <name>.unified.xml in res://assets/xml_unified/, then falls back to the sidecar dir.
func _resolve_xml_path(source_xml: String) -> String:
	if source_xml.is_empty():
		return ""
	# "smoke.pte.xml".get_basename() → "smoke.pte" → "smoke.pte.unified.xml"
	var unified_name := source_xml.get_basename() + ".unified.xml"
	var canonical := "res://assets/xml_unified/".path_join(unified_name)
	if FileAccess.file_exists(canonical):
		return canonical
	if not sidecar_file.is_empty():
		var sidecar_local := sidecar_file.get_base_dir().path_join(unified_name)
		if FileAccess.file_exists(sidecar_local):
			return sidecar_local
	return ""


func _read_sidecar() -> Dictionary:
	if not FileAccess.file_exists(sidecar_file):
		push_warning("CFWCompoundSceneBuilder: sidecar not found: %s" % sidecar_file)
		return {}
	var parsed = JSON.parse_string(FileAccess.get_file_as_string(sidecar_file))
	if not parsed is Dictionary:
		push_warning("CFWCompoundSceneBuilder: sidecar JSON is not an object.")
		return {}
	return parsed


func _find_node_by_name(root: Node, target: String) -> Node:
	if target.is_empty():
		return null
	if root.name == target:
		return root
	for child in root.get_children():
		var found := _find_node_by_name(child, target)
		if found != null:
			return found
	return null


func _clear_generated() -> void:
	for child in get_children():
		if child.has_meta("cfw_generated") or child.name == "Model":
			remove_child(child)
			child.queue_free()


func _set_generated_owner(node: Node) -> void:
	if not Engine.is_editor_hint():
		return
	var scene_root := get_tree().edited_scene_root
	if scene_root != null:
		node.owner = scene_root


func _make_editable_instance(node: Node) -> void:
	if not Engine.is_editor_hint():
		return
	var scene_root := get_tree().edited_scene_root
	if scene_root != null:
		scene_root.set_editable_instance(node, true)
