@tool
extends RefCounted


static func decode(data: Dictionary, texture_name: String) -> Dictionary:
	if data.is_empty():
		return {"texture": null, "size": Vector2i.ZERO}

	var image := decode_embedded_image(data, texture_name)
	if image == null:
		image = decode_legacy_texture(data, texture_name)
	if image == null:
		return {"texture": null, "size": Vector2i.ZERO}

	return {
		"texture": ImageTexture.create_from_image(image),
		"size": Vector2i(image.get_width(), image.get_height()),
	}


static func decode_embedded_image(data: Dictionary, texture_name: String) -> Image:
	var images: Array = data.get("images", [])
	if images.is_empty():
		return null

	var chosen := choose_named(images, texture_name)
	if str(chosen.get("format", "")).to_lower() != "bmp":
		return null

	var files: Dictionary = chosen.get("files", {})
	return decode_bmp32(b64(str(files.get("Image BMP", ""))))


static func decode_legacy_texture(data: Dictionary, texture_name: String) -> Image:
	var textures: Array = data.get("textures", [])
	if textures.is_empty():
		return null

	var chosen := choose_named(textures, texture_name)
	var files: Dictionary = chosen.get("files", {})
	var width := bytes_i32(b64(files.get("Image X size", "")))
	var height := bytes_i32(b64(files.get("Image Y size", "")))
	var indices := b64(first_matching(files, "Image indices"))
	var palette := b64(first_matching(files, "Palette RGB 888"))
	var alpha := b64(first_matching(files, "Alpha 8 bit"))

	if width <= 0 or height <= 0 or indices.is_empty() or palette.is_empty():
		return null

	var image := Image.create(width, height, false, Image.FORMAT_RGBA8)
	for y in range(height):
		for x in range(width):
			var i := y * width + x
			var palette_index := indices[i] if i < indices.size() else 0
			var p := palette_index * 3
			var r := palette[p] if p < palette.size() else 0
			var g := palette[p + 1] if p + 1 < palette.size() else 0
			var b := palette[p + 2] if p + 2 < palette.size() else 0
			var luminance = max(max(r, g), b)
			var a = alpha[i] if i < alpha.size() else luminance
			image.set_pixel(x, y, Color(r / 255.0, g / 255.0, b / 255.0, a / 255.0))
	return image


static func choose_named(items: Array, texture_name: String) -> Dictionary:
	var wanted := texture_name.to_lower()
	var wanted_stem := wanted.get_basename()
	for item in items:
		var name := str(item.get("name", "")).to_lower()
		if name == wanted or name.get_basename() == wanted_stem:
			return item
	return items[0] if not items.is_empty() else {}


static func decode_bmp32(bytes: PackedByteArray) -> Image:
	if bytes.size() < 54 or bytes[0] != 0x42 or bytes[1] != 0x4d:
		return null

	var pixel_offset := bytes_u32_at(bytes, 10)
	var dib_size := bytes_u32_at(bytes, 14)
	if dib_size < 40:
		return null

	var width := bytes_i32_at(bytes, 18)
	var signed_height := bytes_i32_at(bytes, 22)
	var height := absi(signed_height)
	var bit_count := bytes_u16_at(bytes, 28)
	var compression := bytes_u32_at(bytes, 30)
	if width <= 0 or height <= 0 or bit_count != 32 or compression != 0:
		return null

	var row_size := width * 4
	if pixel_offset + row_size * height > bytes.size():
		return null

	var top_down := signed_height < 0
	var image := Image.create(width, height, false, Image.FORMAT_RGBA8)
	for y in range(height):
		var src_y := y if top_down else height - 1 - y
		var row_offset := pixel_offset + src_y * row_size
		for x in range(width):
			var src := row_offset + x * 4
			var b := bytes[src]
			var g := bytes[src + 1]
			var r := bytes[src + 2]
			var a := bytes[src + 3]
			image.set_pixel(x, y, Color(r / 255.0, g / 255.0, b / 255.0, a / 255.0))
	return image


static func first_matching(files: Dictionary, suffix: String) -> String:
	for key in files.keys():
		if str(key).ends_with(suffix):
			return files[key]
	return ""


static func b64(text: String) -> PackedByteArray:
	return Marshalls.base64_to_raw(text.replace("\n", "").replace("\r", "").replace("\t", "").replace(" ", ""))


static func bytes_i32(bytes: PackedByteArray) -> int:
	return bytes_i32_at(bytes, 0)


static func bytes_u16_at(bytes: PackedByteArray, offset: int) -> int:
	if offset < 0 or offset + 1 >= bytes.size():
		return 0
	return bytes[offset] | (bytes[offset + 1] << 8)


static func bytes_u32_at(bytes: PackedByteArray, offset: int) -> int:
	if offset < 0 or offset + 3 >= bytes.size():
		return 0
	return bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16) | (bytes[offset + 3] << 24)


static func bytes_i32_at(bytes: PackedByteArray, offset: int) -> int:
	var value := bytes_u32_at(bytes, offset)
	if value >= 0x80000000:
		return value - 0x100000000
	return value
