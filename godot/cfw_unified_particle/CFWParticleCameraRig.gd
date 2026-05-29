extends Node3D

@export_node_path("Camera3D") var camera_path: NodePath = ^"Camera3D"
@export_node_path("Node3D") var target_path: NodePath = ^"Target"
@export var orbit_sensitivity := 0.006
@export var pan_sensitivity := 0.012
@export var zoom_step := 0.12
@export var min_distance := 1.0
@export var max_distance := 80.0
@export var distance := 18.0
@export var yaw_degrees := -45.0
@export var pitch_degrees := -35.0
@export_group("Debug Helpers")
@export_node_path("Node3D") var center_marker_path: NodePath = ^"CenterMarker"
@export_node_path("Node3D") var ground_path: NodePath = ^"Ground"
@export var show_center_marker := false:
	set(value):
		show_center_marker = value
		_set_helper_visible(center_marker_path, value)
@export var show_ground := false:
	set(value):
		show_ground = value
		_set_helper_visible(ground_path, value)

var _camera: Camera3D
var _target_node: Node3D
var _target := Vector3.ZERO
var _orbiting := false
var _panning := false


func _ready() -> void:
	_camera = get_node_or_null(camera_path) as Camera3D
	_target_node = get_node_or_null(target_path) as Node3D
	if _target_node:
		_target = _target_node.global_position
	_set_helper_visible(center_marker_path, show_center_marker)
	_set_helper_visible(ground_path, show_ground)
	_apply_camera()


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		_handle_mouse_button(event)
	elif event is InputEventMouseMotion:
		_handle_mouse_motion(event)
	elif event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_F:
		_focus_origin()


func _handle_mouse_button(event: InputEventMouseButton) -> void:
	if event.button_index == MOUSE_BUTTON_RIGHT:
		_orbiting = event.pressed
		_set_mouse_capture(_orbiting or _panning)
	elif event.button_index == MOUSE_BUTTON_MIDDLE:
		_panning = event.pressed
		_set_mouse_capture(_orbiting or _panning)
	elif event.pressed and event.button_index == MOUSE_BUTTON_WHEEL_UP:
		distance = maxf(min_distance, distance * (1.0 - zoom_step))
		_apply_camera()
	elif event.pressed and event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
		distance = minf(max_distance, distance * (1.0 + zoom_step))
		_apply_camera()


func _handle_mouse_motion(event: InputEventMouseMotion) -> void:
	if _orbiting:
		yaw_degrees -= rad_to_deg(event.relative.x * orbit_sensitivity)
		pitch_degrees -= rad_to_deg(event.relative.y * orbit_sensitivity)
		pitch_degrees = clampf(pitch_degrees, -85.0, -5.0)
		_apply_camera()
	elif _panning and _camera:
		var right := _camera.global_transform.basis.x
		var up := _camera.global_transform.basis.y
		var scale := distance * pan_sensitivity
		_target += (-right * event.relative.x + up * event.relative.y) * scale
		if _target_node:
			_target_node.global_position = _target
		_apply_camera()


func _apply_camera() -> void:
	if _camera == null:
		return

	var yaw := deg_to_rad(yaw_degrees)
	var pitch := deg_to_rad(pitch_degrees)
	var horizontal := cos(pitch) * distance
	var offset := Vector3(
		sin(yaw) * horizontal,
		sin(-pitch) * distance,
		cos(yaw) * horizontal
	)
	_camera.global_position = _target + offset
	_camera.look_at(_target, Vector3.UP)


func _focus_origin() -> void:
	_target = Vector3.ZERO
	if _target_node:
		_target_node.global_position = _target
	_apply_camera()


func _set_mouse_capture(captured: bool) -> void:
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if captured else Input.MOUSE_MODE_VISIBLE


func _set_helper_visible(path: NodePath, visible: bool) -> void:
	if not is_inside_tree():
		return
	var node := get_node_or_null(path) as Node3D
	if node:
		node.visible = visible
