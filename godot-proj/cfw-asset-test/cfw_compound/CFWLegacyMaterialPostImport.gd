@tool
extends EditorScenePostImport

const ADDITIVE_ALPHA := 1.0

func _post_import(scene: Node) -> Object:
	_apply_legacy_materials(scene)
	return scene


func _apply_legacy_materials(node: Node) -> void:
	if node is MeshInstance3D:
		_apply_mesh_instance_materials(node)

	for child in node.get_children():
		_apply_legacy_materials(child)


func _apply_mesh_instance_materials(mesh_instance: MeshInstance3D) -> void:
	var mesh := mesh_instance.mesh
	if mesh == null:
		return

	for surface_index in mesh.get_surface_count():
		var material := mesh_instance.get_surface_override_material(surface_index)
		if material == null:
			material = mesh.surface_get_material(surface_index)
		var converted := _convert_material(material)
		if converted != null:
			mesh_instance.set_surface_override_material(surface_index, converted)


func _convert_material(material: Material) -> Material:
	if not material is BaseMaterial3D:
		return material

	var base := (material as BaseMaterial3D).duplicate(true) as BaseMaterial3D
	base.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED

	var emission_texture := base.emission_texture
	if emission_texture != null:
		base.next_pass = _make_emissive_overlay(base, emission_texture)

	return base


func _make_emissive_overlay(source: BaseMaterial3D, emission_texture: Texture2D) -> StandardMaterial3D:
	var overlay := StandardMaterial3D.new()
	overlay.resource_name = "%s_additive_emission" % source.resource_name
	overlay.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	overlay.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	overlay.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	overlay.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED
	overlay.no_depth_test = true
	overlay.cull_mode = source.cull_mode
	overlay.albedo_texture = emission_texture
	overlay.albedo_color = Color(1.0, 1.0, 1.0, ADDITIVE_ALPHA)
	return overlay
