shader_type spatial;
render_mode unshaded, blend_mix, depth_prepass_alpha, cull_disabled;

uniform sampler2D albedo_texture : source_color, filter_linear_mipmap, repeat_enable;
uniform sampler2D emission_texture : source_color, filter_linear_mipmap, repeat_enable;
uniform vec4 albedo_tint : source_color = vec4(1.0);
uniform float emission_strength = 0.0;
uniform vec3 light_direction = vec3(-0.3416, -0.7591, -0.4706);
uniform vec3 light_color = vec3(1.05, 1.0, 0.92);
uniform vec3 ambient_color = vec3(0.32, 0.38, 0.48);

void fragment() {
	vec4 texel = texture(albedo_texture, UV);
	vec3 world_normal = normalize((INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);
	float diffuse = max(dot(world_normal, normalize(-light_direction)), 0.0);
	float wrap_light = diffuse * 0.82 + 0.18;
	vec3 lit = texel.rgb * albedo_tint.rgb * (ambient_color + light_color * wrap_light);
	lit += texture(emission_texture, UV).rgb * emission_strength;
	ALBEDO = clamp(lit, vec3(0.0), vec3(1.0));
	ALPHA = texel.a * albedo_tint.a;
}
