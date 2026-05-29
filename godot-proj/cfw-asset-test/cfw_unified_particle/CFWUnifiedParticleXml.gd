@tool
extends RefCounted

const D3DBLEND_ONE := 2


static func load(path: String) -> Dictionary:
	if path == "" or not FileAccess.file_exists(path):
		return {}

	var parser := XMLParser.new()
	var err := parser.open(path)
	if err != OK:
		return {}

	var out := {
		"rendering": {},
		"emitter": {},
		"particles": {},
		"colorFrames": [],
		"colorKeyFrameBits": "0x80000001",
		"runtime": {},
		"images": [],
		"textures": [],
	}

	var stack: Array[String] = []
	var dir_stack: Array[String] = []
	var current_texture := {}
	var current_image := {}
	var current_format := ""
	var current_mip := ""
	var current_file := ""

	while parser.read() == OK:
		var node_type := parser.get_node_type()
		if node_type == XMLParser.NODE_ELEMENT:
			var name := parser.get_node_name()
			var attrs := xml_attrs(parser)
			stack.append(name)

			if name == "runtime":
				out["runtime"] = {
					"endBehavior": attrs.get("endBehavior", "Legacy Source"),
					"zeroParticleLifetimeSeconds": parse_float(attrs.get("zeroParticleLifetimeSeconds", "0")),
				}
			elif name == "rendering":
				out["rendering"] = {
					"textureName": attrs.get("textureName", ""),
					"textureFps": parse_float(attrs.get("textureFps", "0")),
					"srcBlend": parse_int(attrs.get("srcBlend", str(D3DBLEND_ONE))),
					"dstBlend": parse_int(attrs.get("dstBlend", str(D3DBLEND_ONE))),
					"boundingSphereRadius": parse_float(attrs.get("boundingSphereRadius", "1")),
				}
			elif name == "emitter":
				out["emitter"] = {
					"initialParticleCount": parse_float(attrs.get("initialParticleCount", "0")),
					"maxParticleCount": parse_int(attrs.get("maxParticleCount", "0")),
					"lifetime": parse_float(attrs.get("lifetime", "0")),
					"frequency": parse_float(attrs.get("frequency", "0")),
					"nozzleSize": parse_float(attrs.get("nozzleSize", "0")),
				}
			elif name == "particles":
				out["particles"] = {
					"lifetime": parse_float(attrs.get("lifetime", "1")),
					"positionRandomizer": parse_float(attrs.get("positionRandomizer", "0")),
					"velocity": parse_float(attrs.get("velocity", "0")),
					"velocityRandomizer": parse_float(attrs.get("velocityRandomizer", "0")),
					"twistVelocity": parse_float(attrs.get("twistVelocity", "0")),
					"size": parse_float(attrs.get("size", "1")),
					"sizeVelocity": parse_float(attrs.get("sizeVelocity", "0")),
				}
			elif name == "direction" and parent(stack) == "emitter":
				out["emitter"]["direction"] = attrs_vec3(attrs, Vector3.FORWARD)
			elif name == "nozzleDamp" and parent(stack) == "emitter":
				out["emitter"]["nozzleDamp"] = attrs_vec3(attrs, Vector3.ZERO)
			elif name == "gravity" and parent(stack) == "particles":
				out["particles"]["gravity"] = attrs_vec3(attrs, Vector3.ZERO)
			elif name == "colorFrames":
				out["colorKeyFrameBits"] = attrs.get("keyFrameBits", "0x80000001")
			elif name == "frame":
				out["colorFrames"].append(Color(
					parse_float(attrs.get("r", "1")),
					parse_float(attrs.get("g", "1")),
					parse_float(attrs.get("b", "1")),
					parse_float(attrs.get("a", "1"))
				))
			elif name == "image" and parent(stack) == "embeddedImages":
				current_image = {
					"name": attrs.get("name", ""),
					"format": attrs.get("format", ""),
					"width": parse_int(attrs.get("width", "0")),
					"height": parse_int(attrs.get("height", "0")),
					"channels": attrs.get("channels", ""),
					"alpha": attrs.get("alpha", ""),
					"files": {},
				}
				out["images"].append(current_image)
			elif name == "dir":
				var dir_name = attrs.get("name", "")
				dir_stack.append(dir_name)
				var parent_dir := dir_stack[dir_stack.size() - 2] if dir_stack.size() >= 2 else ""
				if parent_dir == "Texture library":
					current_texture = {"name": dir_name, "formats": {}}
					out["textures"].append(current_texture)
				elif dir_name.begins_with("MIP"):
					current_mip = dir_name
				elif dir_name.to_lower() in ["palette 8 bit", "true rgb 565", "true 8 bit"] or dir_name.begins_with("Format_"):
					current_format = dir_name
			elif name == "file":
				current_file = attrs.get("name", "")

			if parser.is_empty():
				if name == "dir" and dir_stack.size() > 0:
					dir_stack.pop_back()
				stack.pop_back()

		elif node_type == XMLParser.NODE_TEXT:
			var text := parser.get_node_data().strip_edges()
			if text != "" and stack.size() > 0 and stack[stack.size() - 1] == "file":
				if not current_image.is_empty():
					capture_image_file(current_image, current_file, text)
				elif not current_texture.is_empty():
					capture_texture_file(current_texture, current_format, current_mip, current_file, text)

		elif node_type == XMLParser.NODE_ELEMENT_END:
			var end_name := parser.get_node_name()
			if end_name == "file":
				current_file = ""
			if end_name == "image":
				current_image = {}
			if end_name == "dir":
				var ended_dir := dir_stack[dir_stack.size() - 1] if dir_stack.size() > 0 else ""
				if not current_texture.is_empty() and ended_dir == current_texture.get("name", ""):
					current_texture = {}
				if ended_dir == current_mip:
					current_mip = ""
				if ended_dir == current_format:
					current_format = ""
				if dir_stack.size() > 0:
					dir_stack.pop_back()
			if stack.size() > 0:
				stack.pop_back()

	while out["colorFrames"].size() < 32:
		out["colorFrames"].append(Color.WHITE)
	if not out["emitter"].has("direction"):
		out["emitter"]["direction"] = vec3_dict(Vector3.FORWARD)
	if not out["emitter"].has("nozzleDamp"):
		out["emitter"]["nozzleDamp"] = vec3_dict(Vector3.ZERO)
	if not out["particles"].has("gravity"):
		out["particles"]["gravity"] = vec3_dict(Vector3.ZERO)

	return out


static func save_current(source_path: String, target_path: String, state: Dictionary) -> Error:
	if target_path == "":
		return ERR_INVALID_PARAMETER

	var xml_text := FileAccess.get_file_as_string(source_path) if FileAccess.file_exists(source_path) else ""
	var parameters_text := build_parameters_xml(state)
	var updated_text := ""
	var start := xml_text.find("<parameters>")
	var end := xml_text.find("</parameters>")
	if start >= 0 and end >= 0:
		end += "</parameters>".length()
		updated_text = xml_text.substr(0, start) + parameters_text + xml_text.substr(end)
	else:
		updated_text = "<?xml version='1.0' encoding='utf-8'?>\n<particleEditor format=\"cfw-unified-particle\" version=\"1\">\n%s\n</particleEditor>\n" % parameters_text

	var file := FileAccess.open(target_path, FileAccess.WRITE)
	if file == null:
		return FileAccess.get_open_error()
	file.store_string(updated_text)
	return OK


static func build_parameters_xml(state: Dictionary) -> String:
	var out := PackedStringArray()
	out.append("<parameters>")
	out.append("    <runtime endBehavior=\"%s\" zeroParticleLifetimeSeconds=\"%s\" />" % [
		xml_escape(str(state.get("endBehavior", "Legacy Source"))),
		fmt(state.get("zeroParticleLifetimeSeconds", 0.0)),
	])
	out.append("    <rendering textureName=\"%s\" textureFps=\"%s\" srcBlend=\"%d\" dstBlend=\"%d\" boundingSphereRadius=\"%s\" />" % [
		xml_escape(str(state.get("textureName", ""))),
		fmt(state.get("textureFps", 0.0)),
		int(state.get("srcBlend", D3DBLEND_ONE)),
		int(state.get("dstBlend", D3DBLEND_ONE)),
		fmt(state.get("boundingSphereRadius", 1.0)),
	])
	out.append("    <emitter initialParticleCount=\"%s\" maxParticleCount=\"%d\" lifetime=\"%s\" frequency=\"%s\" nozzleSize=\"%s\">" % [
		fmt(state.get("initialParticleCount", 0.0)),
		int(state.get("maxParticleCount", 0)),
		fmt(state.get("emitterLifetime", 0.0)),
		fmt(state.get("frequency", 0.0)),
		fmt(state.get("nozzleSize", 0.0)),
	])
	var direction: Vector3 = state.get("direction", Vector3.FORWARD)
	var damp: Vector3 = state.get("nozzleDamp", Vector3.ZERO)
	out.append("      <direction x=\"%s\" y=\"%s\" z=\"%s\" />" % [fmt(direction.x), fmt(direction.y), fmt(direction.z)])
	out.append("      <nozzleDamp x=\"%s\" y=\"%s\" z=\"%s\" />" % [fmt(damp.x), fmt(damp.y), fmt(damp.z)])
	out.append("    </emitter>")
	out.append("    <particles lifetime=\"%s\" positionRandomizer=\"%s\" velocity=\"%s\" velocityRandomizer=\"%s\" twistVelocity=\"%s\" size=\"%s\" sizeVelocity=\"%s\">" % [
		fmt(state.get("particleLifetime", 1.0)),
		fmt(state.get("positionRandomizer", 0.0)),
		fmt(state.get("velocity", 0.0)),
		fmt(state.get("velocityRandomizer", 0.0)),
		fmt(state.get("twistVelocity", 0.0)),
		fmt(state.get("size", 1.0)),
		fmt(state.get("sizeVelocity", 0.0)),
	])
	var gravity: Vector3 = state.get("gravity", Vector3.ZERO)
	out.append("      <gravity x=\"%s\" y=\"%s\" z=\"%s\" />" % [fmt(gravity.x), fmt(gravity.y), fmt(gravity.z)])
	out.append("    </particles>")
	out.append("    <colorFrames keyFrameBits=\"%s\">" % str(state.get("colorKeyFrameBits", "0x80000001")))
	var color_frames: Array = state.get("colorFrames", [])
	for i in range(32):
		var frame: Color = color_frames[i] if i < color_frames.size() else Color.WHITE
		var key := color_frame_is_key(state.get("colorKeyFrameBits", "0x80000001"), i)
		out.append("      <frame index=\"%d\" key=\"%s\" r=\"%s\" g=\"%s\" b=\"%s\" a=\"%s\" />" % [
			i,
			"true" if key else "false",
			fmt(frame.r),
			fmt(frame.g),
			fmt(frame.b),
			fmt(frame.a),
		])
	out.append("    </colorFrames>")
	out.append("  </parameters>")
	return "\n".join(out)


static func fmt(value) -> String:
	var number := to_float(value, 0.0)
	if is_nan(number) or is_inf(number):
		return "0"
	var text := "%.9f" % number
	while text.ends_with("0") and text.find(".") >= 0:
		text = text.substr(0, text.length() - 1)
	if text.ends_with("."):
		text = text.substr(0, text.length() - 1)
	if text == "-0":
		return "0"
	return text


static func parse_float(value) -> float:
	return to_float(value, 0.0)


static func parse_int(value) -> int:
	var text := str(value)
	if text.begins_with("0x"):
		return text.hex_to_int()
	return int(text)


static func color_frame_is_key(bits_value, index: int) -> bool:
	var bits = bits_value
	if typeof(bits) == TYPE_STRING:
		bits = parse_int(bits)
	return (int(bits) & (1 << index)) != 0


static func xml_attrs(parser: XMLParser) -> Dictionary:
	var attrs := {}
	for i in range(parser.get_attribute_count()):
		attrs[parser.get_attribute_name(i)] = parser.get_attribute_value(i)
	return attrs


static func parent(stack: Array[String]) -> String:
	return stack[stack.size() - 2] if stack.size() >= 2 else ""


static func attrs_vec3(attrs: Dictionary, fallback: Vector3) -> Dictionary:
	return {
		"x": parse_float(attrs.get("x", str(fallback.x))),
		"y": parse_float(attrs.get("y", str(fallback.y))),
		"z": parse_float(attrs.get("z", str(fallback.z))),
	}


static func vec3_dict(value: Vector3) -> Dictionary:
	return {"x": value.x, "y": value.y, "z": value.z}


static func capture_texture_file(current_texture: Dictionary, current_format: String, current_mip: String, file_name: String, text: String) -> void:
	if file_name == "":
		return
	if not current_texture.has("files"):
		current_texture["files"] = {}
	var key := "%s/%s/%s" % [current_format, current_mip, file_name]
	current_texture["files"][key] = str(current_texture["files"].get(key, "")) + text
	current_texture["files"][file_name] = str(current_texture["files"].get(file_name, "")) + text


static func capture_image_file(current_image: Dictionary, file_name: String, text: String) -> void:
	if file_name == "":
		return
	var files: Dictionary = current_image.get("files", {})
	files[file_name] = str(files.get(file_name, "")) + text
	current_image["files"] = files


static func xml_escape(value: String) -> String:
	return value.replace("&", "&amp;").replace("\"", "&quot;").replace("<", "&lt;").replace(">", "&gt;")


static func to_float(value, fallback: float) -> float:
	match typeof(value):
		TYPE_FLOAT, TYPE_INT:
			return value
		TYPE_STRING, TYPE_STRING_NAME:
			var text := str(value).strip_edges()
			return fallback if text == "" else float(text)
		_:
			return fallback
