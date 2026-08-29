@tool
extends Node3D

@export_file("*.gltf", "*.glb", "*.tscn", "*.scn") var model_scene_file := ""
@export_file("*.json") var sidecar_file := ""
@export var particle_player_scene: PackedScene = preload("res://cfw_unified_particle/CFWUnifiedParticlePlayer.tscn")
@export_range(1.0, 1000.0, 1.0) var source_scale_divider := 100.0
@export_range(0.0001, 1000.0, 0.0001) var particle_size_multiplier := 10.0

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
	return
	_clear_generated()
	if model_scene_file.is_empty():
		push_warning("No model_scene_file set.")
		return

	var packed := ResourceLoader.load(model_scene_file) as PackedScene
	if packed == null:
		push_warning("Could not load model scene: %s" % model_scene_file)
		return

	var model := packed.instantiate() as Node3D
	if model == null:
		push_warning("Model scene root is not Node3D: %s" % model_scene_file)
		return

	model.name = "Model"
	model.set_meta("cfw_compound_generated", true)
	model.scale = Vector3.ONE / source_scale_divider
	add_child(model)
	_set_owner_recursive(model)

	if not sidecar_file.is_empty():
		var particles_root := Node3D.new()
		particles_root.name = "Particles"
		particles_root.set_meta("cfw_compound_generated", true)
		add_child(particles_root)
		_set_owner_recursive(particles_root)
		_force_update_transforms(model)
		_attach_particles(model, particles_root)


func _attach_particles(model: Node3D, particles_root: Node3D) -> void:
	if particle_player_scene == null:
		push_warning("No particle_player_scene set.")
		return

	var data := _read_sidecar()
	if data.is_empty():
		return

	for attachment in data.get("particleAttachments", []):
		if not attachment is Dictionary:
			continue

		var parent_name := str(attachment.get("parentName", ""))
		var parent := _find_node_by_name(model, parent_name)
		if parent == null:
			parent = model

		var particle := particle_player_scene.instantiate() as Node3D
		if particle == null:
			continue

		particle.name = str(attachment.get("objectName", "particle"))
		particle.set_meta("cfw_compound_generated", true)
		var local_transform := _transform_from_sidecar(attachment.get("localTransform", {}))
		particle.transform = particles_root.global_transform.affine_inverse() * parent.global_transform * local_transform
		var source_unit_scale := 1.0 / source_scale_divider
		particle.set("world_scale", source_unit_scale)
		particle.set("size_scale", particle_size_multiplier)
		particle.set("unified_xml_file", str(attachment.get("unifiedXml", "")))
		if particle.has_method("reload"):
			particle.call("reload")
		_apply_particle_size_multiplier(particle)

		particles_root.add_child(particle)
		_set_owner_recursive(particle)


func _read_sidecar() -> Dictionary:
	if not FileAccess.file_exists(sidecar_file):
		push_warning("Sidecar file does not exist: %s" % sidecar_file)
		return {}

	var text := FileAccess.get_file_as_string(sidecar_file)
	var parsed = JSON.parse_string(text)
	if not parsed is Dictionary:
		push_warning("Sidecar JSON is not an object: %s" % sidecar_file)
		return {}
	return parsed


func _apply_particle_size_multiplier(particle: Node3D) -> void:
	particle.set("particle_size", float(particle.get("particle_size")) * particle_size_multiplier)
	particle.set("particle_size_velocity", float(particle.get("particle_size_velocity")) * particle_size_multiplier)
	if particle.has_method("restart"):
		particle.call("restart")


func _transform_from_sidecar(value) -> Transform3D:
	if not value is Dictionary:
		return Transform3D.IDENTITY

	var rows: Array = value.get("basisRows", [])
	var origin_values: Array = value.get("origin", [])
	if rows.size() < 3 or origin_values.size() < 3:
		return Transform3D.IDENTITY

	var r0 := _array_to_vec3(rows[0])
	var r1 := _array_to_vec3(rows[1])
	var r2 := _array_to_vec3(rows[2])
	var origin := _array_to_vec3(origin_values)
	var basis := Basis(
		Vector3(r0.x, r1.x, r2.x),
		Vector3(r0.y, r1.y, r2.y),
		Vector3(r0.z, r1.z, r2.z)
	)
	basis = _source_attachment_basis_to_godot(basis)
	return Transform3D(basis, origin)


func _array_to_vec3(value) -> Vector3:
	if not value is Array or value.size() < 3:
		return Vector3.ZERO
	return Vector3(float(value[0]), float(value[1]), float(value[2]))


func _source_attachment_basis_to_godot(basis: Basis) -> Basis:
	var correction := Basis(
		Vector3(0.0, 1.0, 0.0),
		Vector3(0.0, 0.0, -1.0),
		Vector3(-1.0, 0.0, 0.0)
	)
	return correction * basis


func _find_node_by_name(root: Node, node_name: String) -> Node3D:
	if node_name.is_empty():
		return null
	if root.name == node_name and root is Node3D:
		return root
	for child in root.get_children():
		var found := _find_node_by_name(child, node_name)
		if found != null:
			return found
	return null


func _clear_generated() -> void:
	for child in get_children():
		if child.has_meta("cfw_compound_generated") or child.name in ["Model", "Particles"]:
			remove_child(child)
			child.queue_free()


func _set_owner_recursive(node: Node) -> void:
	if Engine.is_editor_hint():
		node.owner = owner if owner != null else self
	for child in node.get_children():
		_set_owner_recursive(child)


func _force_update_transforms(node: Node) -> void:
	if node is Node3D:
		(node as Node3D).force_update_transform()
	for child in node.get_children():
		_force_update_transforms(child)
