#version 460

layout(location = 0) in vec3 in_world_pos;
layout(location = 1) in vec2 in_texcoord;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec2 out_normal; // color attachment 0

layout(binding = 0) uniform sampler2D u_albedo_texture;

uniform vec3 u_cam_pos;
uniform float u_far_z;

uniform float u_shadow_bias_constant;
uniform float u_shadow_bias_slope_scale;
uniform float u_shadow_bias_distance_scale;
uniform float u_shadow_bias_slope_power;
uniform float u_shadow_bias_scale;

vec2 encodeNormal(vec3 normal);
float computeFragmentBias(float normalized_depth, vec3 light_to_frag, vec3 depth_normal);

void main()
{
	// simple alpha test, discard fragment if fransparent "enough"
	float alpha = texture(u_albedo_texture, in_texcoord).a;
	if (alpha < 0.5) discard;

	// manual linearized, radial depth
	float dist = distance(in_world_pos, u_cam_pos);  // assuming 'light.position' is available or passed as uniform

	float normalized_depth = dist / u_far_z;

	// apply bias
	normalized_depth += computeFragmentBias(normalized_depth, in_world_pos - u_cam_pos, in_normal);

	gl_FragDepth = normalized_depth;  // [0, 1] depth relative to light range
    out_normal = encodeNormal(in_normal);
}

vec2 encodeNormal(vec3 n)
{
	// return n.xy;

	// octahedral encoding
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 enc = n.xy;
    if (n.z < 0)
        enc = (1 - abs(enc.yx)) * sign(enc.xy);
    return enc * 0.5 + 0.5; // Map from [-1,1] to [0,1]
}

float computeSlopeBias(vec3 light_to_frag, vec3 depth_normal)
{
	vec3 light_dir = normalize(-light_to_frag);
	float angle = dot(depth_normal, light_dir);
	angle = pow(angle, u_shadow_bias_slope_power);
	angle = clamp(angle, 0.0, 0.99);
	return (0.001 / angle) * u_shadow_bias_slope_scale;
}

float computeFragmentBias(float normalized_depth, vec3 light_to_frag, vec3 depth_normal)
{
	// calculate shadow depth bias by various factors
	// bias by depth
	float bias = normalized_depth * u_shadow_bias_distance_scale;

	// bias based on surface normals
	// bias increases when the angle between normal and light_dir is steep.
	if(u_shadow_bias_slope_scale > 0)
		bias += computeSlopeBias(light_to_frag, depth_normal);

	bias = clamp(bias, -0.05, 0.05) * u_shadow_bias_scale;
	return bias + u_shadow_bias_constant;
}
