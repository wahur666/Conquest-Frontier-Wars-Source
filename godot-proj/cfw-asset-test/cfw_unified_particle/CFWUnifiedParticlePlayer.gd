@tool
extends Node3D

const CFWUnifiedParticleXml = preload("res://cfw_unified_particle/CFWUnifiedParticleXml.gd")
const CFWParticleTextureDecoder = preload("res://cfw_unified_particle/CFWParticleTextureDecoder.gd")

const D3DBLEND_ZERO := 1
const D3DBLEND_ONE := 2
const D3DBLEND_SRCALPHA := 5
const D3DBLEND_INVSRCALPHA := 6

enum BillboardMode {
	FIXED,
	CAMERA_FACING,
}

enum EndBehavior {
	LEGACY_SOURCE,
	STOP_AT_EMITTER_END,
	DRAIN_PARTICLES,
}

@export_file("*.xml") var unified_xml_file: String = "":
	set(value):
		unified_xml_file = value
		reload()

@export var preview_running := true:
	set(value):
		preview_running = value
		if _multimesh_instance:
			_multimesh_instance.visible = value and show_cpu_particles
		if value:
			restart()

@export var auto_restart := true

@export var restart_preview := false:
	set(value):
		restart_preview = false
		if value:
			restart()
			notify_property_list_changed()

@export var reload_preview := false:
	set(value):
		reload_preview = false
		if value:
			reload()
			notify_property_list_changed()

@export var world_scale := 0.01:
	set(value):
		world_scale = maxf(value, 0.0001)
		restart()

@export var size_scale := 0.01:
	set(value):
		size_scale = maxf(value, 0.0001)
		restart()

@export var random_seed := 1:
	set(value):
		random_seed = value
		restart()

@export var max_preview_particles := 20000:
	set(value):
		max_preview_particles = clampi(value, 1, 100000)
		reload()

@export var apply_inspector_values := false:
	set(value):
		apply_inspector_values = false
		if value:
			_apply_inspector_values()
			notify_property_list_changed()

@export_group("Authoring")
@export_enum("Legacy Source", "Stop At Emitter End", "Drain Particles") var end_behavior: int = EndBehavior.LEGACY_SOURCE:
	set(value):
		end_behavior = value
		restart()

@export var zero_particle_lifetime_seconds := 0.0:
	set(value):
		zero_particle_lifetime_seconds = maxf(value, 0.0)
		restart()

@export_file("*.xml") var save_xml_file := ""

@export var save_xml := false:
	set(value):
		save_xml = false
		if value:
			save_current_xml()
			notify_property_list_changed()

@export_enum("Fixed", "Camera Facing") var billboard_mode: int = BillboardMode.CAMERA_FACING:
	set(value):
		billboard_mode = value
		if _multimesh:
			_sync_multimesh()

@export_group("Debug")
@export var debug_force_visible := false:
	set(value):
		debug_force_visible = value
		if _multimesh:
			_sync_multimesh()

@export var debug_trace := false

@export var show_cpu_particles := true:
	set(value):
		show_cpu_particles = value
		if _multimesh_instance:
			_multimesh_instance.visible = preview_running and value

@export_group("XML Rendering")
@export var render_texture_name := ""
@export var render_texture_fps := 0.0
@export var render_src_blend := D3DBLEND_ONE
@export var render_dst_blend := D3DBLEND_ONE
@export var render_bounding_sphere_radius := 1.0
@export var decoded_texture_size := Vector2i.ZERO
@export var embedded_texture_count := 0

@export_group("XML Emitter")
@export var emitter_initial_particle_count := 0.0
@export var emitter_max_particle_count := 0
@export var emitter_lifetime := 0.0
@export var emitter_frequency := 0.0
@export var emitter_nozzle_size := 0.0
@export var emitter_direction := Vector3.FORWARD
@export var emitter_nozzle_damp := Vector3.ZERO

@export_group("XML Particles")
@export var particle_lifetime := 1.0
@export var particle_position_randomizer := 0.0
@export var particle_velocity := 0.0
@export var particle_velocity_randomizer := 0.0
@export var particle_twist_velocity := 0.0
@export var particle_size := 1.0
@export var particle_size_velocity := 0.0
@export var particle_gravity := Vector3.ZERO
@export var color_frames: Array[Color] = []

var parameters := {}
var _particles: Array = []
var _spawn_accumulator := 0.0
var _created_particles := 0
var _elapsed := 0.0
var _emitter_lifetime := 0.0
var _finished := false
var _rng := RandomNumberGenerator.new()
var _multimesh_instance: MultiMeshInstance3D
var _multimesh: MultiMesh
var _texture: Texture2D


func _ready() -> void:
	set_process(true)
	reload()


func _process(delta: float) -> void:
	if not preview_running or parameters.is_empty():
		return
	_step(minf(maxf(delta, 0.0), 0.05))
	if auto_restart and _finished:
		restart()
	_sync_multimesh()


func reload() -> void:
	if not is_inside_tree():
		return
	parameters = CFWUnifiedParticleXml.load(_resolve_path(unified_xml_file))
	if parameters.is_empty():
		_trace("No parameters loaded. Set unified_xml_file to a *.pte.unified.xml file.")
	_copy_parameters_to_exports()
	_texture = _decode_embedded_texture(parameters)
	_setup_multimesh()
	restart()
	notify_property_list_changed()


func restart() -> void:
	_rng.seed = int(random_seed)
	_particles.clear()
	_created_particles = 0
	_elapsed = 0.0
	_finished = false
	_spawn_accumulator = maxf(0.0, _emitter().get("initialParticleCount", 0.0))
	_emitter_lifetime = _emitter().get("lifetime", 0.0)
	if _multimesh:
		_multimesh.visible_instance_count = 0
		if debug_force_visible and parameters.is_empty():
			_write_debug_particle()
	if not parameters.is_empty():
		var initial := int(floor(maxf(0.0, _emitter().get("initialParticleCount", 0.0))))
		for _i in range(mini(initial, _max_particles())):
			_spawn_particle()
		_spawn_accumulator = maxf(0.0, _emitter().get("initialParticleCount", 0.0)) - initial
		_sync_multimesh()
		_trace("Restarted: initial=%d live=%d max=%d texture=%s" % [initial, _particles.size(), _max_particles(), str(_texture != null)])


func _step(dt: float) -> void:
	var p := _particle_params()
	var emitter := _emitter()
	_elapsed += dt

	if dt > 0.0 and emitter.get("lifetime", 0.0) > 0.0:
		_emitter_lifetime -= dt

	for i in range(_particles.size() - 1, -1, -1):
		var particle: Dictionary = _particles[i]
		_update_particle(particle, p, dt)
		if _particle_has_finite_lifetime(p) and particle["lifetime"] <= 0.0:
			_particles.remove_at(i)

	var can_create = emitter.get("lifetime", 0.0) <= 0.0 or _emitter_lifetime > 0.0
	if can_create:
		_spawn_accumulator += maxf(0.0, emitter.get("frequency", 0.0)) * dt
		var count := int(floor(_spawn_accumulator))
		_spawn_accumulator -= count

		var source_max := int(emitter.get("maxParticleCount", 0))
		if source_max > 0:
			count = mini(count, maxi(0, source_max - _created_particles))

		for _i in range(count):
			_spawn_particle()

	_update_finished_state(emitter)


func _spawn_particle() -> void:
	if _particles.size() >= _max_particles():
		_particles.remove_at(0)

	var emitter := _emitter()
	var p := _particle_params()
	var velocity_random := _rng.randf() * maxf(0.0, p.get("velocityRandomizer", 0.0))
	var velocity = p.get("velocity", 0.0) + p.get("velocity", 0.0) * velocity_random
	var position := Vector3.ZERO

	if p.get("positionRandomizer", 0.0) != 0.0:
		var r = p.get("positionRandomizer", 0.0) * world_scale
		position += Vector3(_frand() * r, _frand() * r, _frand() * r)

	_particles.append({
		"position": position,
		"direction": _make_emitter_direction(emitter),
		"velocity": velocity * world_scale,
		"size": p.get("size", 1.0) * size_scale,
		"lifetime": _effective_particle_lifetime(p),
		"age": 0.0,
	})
	_created_particles += 1


func _update_particle(particle: Dictionary, p: Dictionary, dt: float) -> void:
	var direction: Vector3 = particle["direction"]
	var twist = p.get("twistVelocity", 0.0)
	if twist != 0.0:
		var amount = twist * dt
		var old_x := direction.x
		direction.x -= direction.y * amount
		direction.y += old_x * amount
		direction = direction.normalized() if direction.length_squared() > 0.000001 else Vector3.FORWARD

	if p.get("sizeVelocity", 0.0) != 0.0:
		particle["size"] = maxf(0.001, particle["size"] + p.get("sizeVelocity", 0.0) * size_scale * dt)

	var gravity := _dict_vec3(p.get("gravity", {})) * world_scale
	direction += gravity * dt
	particle["direction"] = direction
	particle["position"] += direction * particle["velocity"] * dt
	particle["lifetime"] -= dt
	particle["age"] += dt


func _particle_has_finite_lifetime(p: Dictionary) -> bool:
	return _effective_particle_lifetime(p) > 0.0


func _effective_particle_lifetime(p: Dictionary) -> float:
	var source_lifetime := float(p.get("lifetime", 0.0))
	if source_lifetime > 0.0:
		return source_lifetime
	if zero_particle_lifetime_seconds > 0.0:
		return zero_particle_lifetime_seconds
	return 0.0


func _update_finished_state(emitter: Dictionary) -> void:
	var emitter_done = emitter.get("lifetime", 0.0) > 0.0 and _emitter_lifetime <= 0.0
	if not emitter_done:
		return

	match end_behavior:
		EndBehavior.STOP_AT_EMITTER_END:
			_particles.clear()
			_finished = true
		EndBehavior.DRAIN_PARTICLES:
			_finished = _particles.is_empty()
		_:
			_finished = _particles.is_empty()


func _sync_multimesh() -> void:
	if _multimesh == null:
		return

	var count := mini(_particles.size(), _max_particles())
	_multimesh.visible_instance_count = count
	if debug_force_visible and count == 0:
		_write_debug_particle()
		return

	var camera := get_viewport().get_camera_3d()

	for i in range(count):
		var particle: Dictionary = _particles[i]
		var size := maxf(0.001, particle["size"])
		var billboard_basis := _particle_billboard_basis(particle["position"], camera)
		var transform := Transform3D(billboard_basis.scaled(Vector3(size, size, size)), particle["position"])
		_multimesh.set_instance_transform(i, transform)
		_multimesh.set_instance_color(i, _particle_color(particle))


func _particle_billboard_basis(position: Vector3, camera: Camera3D) -> Basis:
	if billboard_mode == BillboardMode.FIXED or camera == null or _multimesh_instance == null:
		return Basis.IDENTITY

	var camera_position := _multimesh_instance.to_local(camera.global_transform.origin)
	var to_camera := camera_position - position
	if to_camera.length_squared() <= 0.000001:
		return Basis.IDENTITY

	var z_axis := to_camera.normalized()
	var camera_up := _multimesh_instance.global_transform.basis.inverse() * camera.global_transform.basis.y
	if camera_up.length_squared() <= 0.000001 or absf(camera_up.normalized().dot(z_axis)) > 0.98:
		camera_up = Vector3.UP

	var x_axis := camera_up.cross(z_axis).normalized()
	var y_axis := z_axis.cross(x_axis).normalized()
	return Basis(x_axis, y_axis, z_axis)


func _make_emitter_direction(emitter: Dictionary) -> Vector3:
	var damp := _dict_vec3(emitter.get("nozzleDamp", {}))
	var direction := _dict_vec3(emitter.get("direction", {"x": 0.0, "y": 0.0, "z": 1.0}))

	if damp.length_squared() <= 0.000001:
		return direction.normalized() if direction.length_squared() > 0.000001 else Vector3.FORWARD

	direction *= emitter.get("nozzleSize", 0.0)
	direction += Vector3(_frand() * damp.x, _frand() * damp.y, _frand() * damp.z)
	return direction.normalized() if direction.length_squared() > 0.000001 else Vector3.FORWARD


func _particle_color(particle: Dictionary) -> Color:
	var frames: Array = color_frames
	if frames.is_empty():
		return Color.WHITE

	var life = _effective_particle_lifetime(_particle_params())
	if life <= 0.0:
		life = maxf(1.0, particle["age"] + particle["lifetime"])

	var t := clampf(particle["age"] / life, 0.0, 1.0)
	var f := t * 31.0
	var i0 := clampi(int(floor(f)), 0, frames.size() - 1)
	var i1 := clampi(i0 + 1, 0, frames.size() - 1)
	var mix_value = f - floor(f)
	var c0: Color = frames[i0]
	var c1: Color = frames[i1]
	var color := c0.lerp(c1, mix_value)
	if color.a <= 0.0 and _uses_additive_blend():
		color.a = 1.0
	return color


func _setup_multimesh() -> void:
	_multimesh_instance = get_node_or_null("Particles") as MultiMeshInstance3D
	if _multimesh_instance == null:
		_trace("Missing child MultiMeshInstance3D named Particles.")
		return

	var quad := QuadMesh.new()
	quad.size = Vector2.ONE
	quad.material = _make_material()

	_multimesh = MultiMesh.new()
	_multimesh.transform_format = MultiMesh.TRANSFORM_3D
	_multimesh.use_colors = true
	_multimesh.mesh = quad
	_multimesh.instance_count = _max_particles() if not parameters.is_empty() else 1
	_multimesh.visible_instance_count = 0
	_multimesh_instance.multimesh = _multimesh
	_multimesh_instance.visible = preview_running and show_cpu_particles
	_multimesh_instance.custom_aabb = AABB(Vector3(-10000, -10000, -10000), Vector3(20000, 20000, 20000))
	_trace("CPU MultiMesh ready: instances=%d" % _multimesh.instance_count)


func _make_material() -> ShaderMaterial:
	var additive := _uses_additive_blend()
	var shader := Shader.new()
	shader.code = _shader_code(additive)

	var mat := ShaderMaterial.new()
	mat.shader = shader
	mat.set_shader_parameter("mask_texture", _texture if _texture else _make_fallback_texture())
	mat.set_shader_parameter("has_texture", _texture != null)
	return mat


func _uses_additive_blend() -> bool:
	var rendering := _rendering()
	return int(rendering.get("srcBlend", D3DBLEND_ONE)) == D3DBLEND_ONE and int(rendering.get("dstBlend", D3DBLEND_ONE)) == D3DBLEND_ONE


func _shader_code(additive: bool) -> String:
	var blend := "blend_add" if additive else "blend_mix"
	return """
shader_type spatial;
render_mode unshaded, cull_disabled, depth_draw_never, %s;

uniform sampler2D mask_texture : source_color;
uniform bool has_texture = false;

void fragment() {
	vec4 mask = has_texture ? texture(mask_texture, UV) : vec4(1.0);
	float shape = max(max(mask.r, mask.g), mask.b);
	ALBEDO = COLOR.rgb * shape;
	ALPHA = COLOR.a * shape;
}
""" % blend


func _max_particles() -> int:
	if parameters.is_empty():
		return 1
	var emitter := _emitter()
	var source_max := int(emitter.get("maxParticleCount", 0))
	if source_max > 0:
		return clampi(source_max, 1, max_preview_particles)

	var p := _particle_params()
	var estimate := int(ceil(maxf(16.0, emitter.get("frequency", 0.0) * _effective_particle_lifetime(p) * 1.25 + emitter.get("initialParticleCount", 0.0))))
	return clampi(estimate, 1, max_preview_particles)


func _emitter() -> Dictionary:
	return {
		"initialParticleCount": emitter_initial_particle_count,
		"maxParticleCount": emitter_max_particle_count,
		"lifetime": emitter_lifetime,
		"frequency": emitter_frequency,
		"nozzleSize": emitter_nozzle_size,
		"direction": _vec3_dict(emitter_direction),
		"nozzleDamp": _vec3_dict(emitter_nozzle_damp),
	}


func _particle_params() -> Dictionary:
	return {
		"lifetime": particle_lifetime,
		"positionRandomizer": particle_position_randomizer,
		"velocity": particle_velocity,
		"velocityRandomizer": particle_velocity_randomizer,
		"twistVelocity": particle_twist_velocity,
		"size": particle_size,
		"sizeVelocity": particle_size_velocity,
		"gravity": _vec3_dict(particle_gravity),
	}


func _rendering() -> Dictionary:
	return {
		"textureName": render_texture_name,
		"textureFps": render_texture_fps,
		"srcBlend": render_src_blend,
		"dstBlend": render_dst_blend,
		"boundingSphereRadius": render_bounding_sphere_radius,
	}


func _copy_parameters_to_exports() -> void:
	if parameters.is_empty():
		render_texture_name = ""
		render_texture_fps = 0.0
		render_src_blend = D3DBLEND_ONE
		render_dst_blend = D3DBLEND_ONE
		render_bounding_sphere_radius = 1.0
		decoded_texture_size = Vector2i.ZERO
		embedded_texture_count = 0
		emitter_initial_particle_count = 0.0
		emitter_max_particle_count = 0
		emitter_lifetime = 0.0
		emitter_frequency = 0.0
		emitter_nozzle_size = 0.0
		emitter_direction = Vector3.FORWARD
		emitter_nozzle_damp = Vector3.ZERO
		particle_lifetime = 1.0
		particle_position_randomizer = 0.0
		particle_velocity = 0.0
		particle_velocity_randomizer = 0.0
		particle_twist_velocity = 0.0
		particle_size = 1.0
		particle_size_velocity = 0.0
		particle_gravity = Vector3.ZERO
		color_frames = []
		end_behavior = EndBehavior.LEGACY_SOURCE
		zero_particle_lifetime_seconds = 0.0
		return

	var runtime: Dictionary = parameters.get("runtime", {})
	end_behavior = _parse_end_behavior(str(runtime.get("endBehavior", "Legacy Source")))
	zero_particle_lifetime_seconds = float(runtime.get("zeroParticleLifetimeSeconds", 0.0))

	var rendering: Dictionary = parameters.get("rendering", {})
	render_texture_name = str(rendering.get("textureName", ""))
	render_texture_fps = float(rendering.get("textureFps", 0.0))
	render_src_blend = int(rendering.get("srcBlend", D3DBLEND_ONE))
	render_dst_blend = int(rendering.get("dstBlend", D3DBLEND_ONE))
	render_bounding_sphere_radius = float(rendering.get("boundingSphereRadius", 1.0))
	var textures: Array = parameters.get("textures", [])
	embedded_texture_count = textures.size()

	var emitter: Dictionary = parameters.get("emitter", {})
	emitter_initial_particle_count = float(emitter.get("initialParticleCount", 0.0))
	emitter_max_particle_count = int(emitter.get("maxParticleCount", 0))
	emitter_lifetime = float(emitter.get("lifetime", 0.0))
	emitter_frequency = float(emitter.get("frequency", 0.0))
	emitter_nozzle_size = float(emitter.get("nozzleSize", 0.0))
	emitter_direction = _dict_vec3(emitter.get("direction", _vec3_dict(Vector3.FORWARD)))
	emitter_nozzle_damp = _dict_vec3(emitter.get("nozzleDamp", _vec3_dict(Vector3.ZERO)))

	var particle_params: Dictionary = parameters.get("particles", {})
	particle_lifetime = float(particle_params.get("lifetime", 1.0))
	particle_position_randomizer = float(particle_params.get("positionRandomizer", 0.0))
	particle_velocity = float(particle_params.get("velocity", 0.0))
	particle_velocity_randomizer = float(particle_params.get("velocityRandomizer", 0.0))
	particle_twist_velocity = float(particle_params.get("twistVelocity", 0.0))
	particle_size = float(particle_params.get("size", 1.0))
	particle_size_velocity = float(particle_params.get("sizeVelocity", 0.0))
	particle_gravity = _dict_vec3(particle_params.get("gravity", _vec3_dict(Vector3.ZERO)))
	color_frames = []
	for frame in parameters.get("colorFrames", []):
		color_frames.append(frame as Color)


func _parse_end_behavior(value: String) -> int:
	match value:
		"Stop At Emitter End":
			return EndBehavior.STOP_AT_EMITTER_END
		"Drain Particles":
			return EndBehavior.DRAIN_PARTICLES
		_:
			return EndBehavior.LEGACY_SOURCE


func _apply_inspector_values() -> void:
	_texture = _decode_embedded_texture(parameters)
	_setup_multimesh()
	restart()


func save_current_xml() -> void:
	if parameters.is_empty():
		_trace("No XML parameters to save.")
		return

	var source_path := _resolve_path(unified_xml_file)
	var target_path := _resolve_path(save_xml_file) if save_xml_file != "" else source_path
	if target_path == "":
		_trace("No XML target path to save.")
		return

	var err := CFWUnifiedParticleXml.save_current(source_path, target_path, _xml_state())
	if err != OK:
		_trace("Could not save unified particle XML: %s error=%d" % [target_path, err])
		return
	_trace("Saved unified particle XML: %s" % target_path)


func _xml_state() -> Dictionary:
	return {
		"endBehavior": _end_behavior_name(),
		"zeroParticleLifetimeSeconds": zero_particle_lifetime_seconds,
		"textureName": render_texture_name,
		"textureFps": render_texture_fps,
		"srcBlend": render_src_blend,
		"dstBlend": render_dst_blend,
		"boundingSphereRadius": render_bounding_sphere_radius,
		"initialParticleCount": emitter_initial_particle_count,
		"maxParticleCount": emitter_max_particle_count,
		"emitterLifetime": emitter_lifetime,
		"frequency": emitter_frequency,
		"nozzleSize": emitter_nozzle_size,
		"direction": emitter_direction,
		"nozzleDamp": emitter_nozzle_damp,
		"particleLifetime": particle_lifetime,
		"positionRandomizer": particle_position_randomizer,
		"velocity": particle_velocity,
		"velocityRandomizer": particle_velocity_randomizer,
		"twistVelocity": particle_twist_velocity,
		"size": particle_size,
		"sizeVelocity": particle_size_velocity,
		"gravity": particle_gravity,
		"colorKeyFrameBits": parameters.get("colorKeyFrameBits", "0x80000001"),
		"colorFrames": color_frames,
	}


func _end_behavior_name() -> String:
	match end_behavior:
		EndBehavior.STOP_AT_EMITTER_END:
			return "Stop At Emitter End"
		EndBehavior.DRAIN_PARTICLES:
			return "Drain Particles"
		_:
			return "Legacy Source"


func _color_frame_is_key(index: int) -> bool:
	return CFWUnifiedParticleXml.color_frame_is_key(parameters.get("colorKeyFrameBits", "0x80000001"), index)


func _fmt(value) -> String:
	return CFWUnifiedParticleXml.fmt(value)


func _frand() -> float:
	return _rng.randf() * 2.0 - 1.0


func _decode_embedded_texture(data: Dictionary) -> Texture2D:
	var decoded := CFWParticleTextureDecoder.decode(data, str(_rendering().get("textureName", "")))
	decoded_texture_size = decoded.get("size", Vector2i.ZERO)
	return decoded.get("texture", null)


func _write_debug_particle() -> void:
	if _multimesh == null:
		return
	_multimesh.visible_instance_count = 1
	_multimesh.set_instance_transform(0, Transform3D(Basis.IDENTITY.scaled(Vector3(100.0, 100.0, 100.0)), Vector3.ZERO))
	_multimesh.set_instance_color(0, Color(1.0, 0.0, 1.0, 1.0))
	_trace("Debug particle written.")


func _make_fallback_texture() -> Texture2D:
	var image := Image.create(8, 8, false, Image.FORMAT_RGBA8)
	for y in range(8):
		for x in range(8):
			var d := Vector2(x - 3.5, y - 3.5).length() / 3.5
			var a := clampf(1.0 - d, 0.0, 1.0)
			image.set_pixel(x, y, Color(1.0, 1.0, 1.0, a))
	return ImageTexture.create_from_image(image)


func _dict_vec3(value) -> Vector3:
	if typeof(value) == TYPE_DICTIONARY:
		return Vector3(float(value.get("x", 0.0)), float(value.get("y", 0.0)), float(value.get("z", 0.0)))
	return Vector3.ZERO


func _vec3_dict(value: Vector3) -> Dictionary:
	return {"x": value.x, "y": value.y, "z": value.z}


func _resolve_path(path: String) -> String:
	if path == "":
		return ""
	if path.begins_with("res://") or path.begins_with("user://"):
		return path
	var base := scene_file_path.get_base_dir()
	if base == "":
		base = "res://"
	return base.path_join(path)


func _trace(message: String) -> void:
	if debug_trace:
		print("[CFWParticle] %s" % message)
