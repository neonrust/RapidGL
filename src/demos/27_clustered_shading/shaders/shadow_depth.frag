#version 460

layout(location = 0) in vec3 in_world_pos;
layout(location = 1) in vec2 in_texcoord;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec2 out_normal; // color attachment 0

layout(binding = 0) uniform sampler2D u_albedo_texture;

uniform vec3 u_cam_pos;
uniform float u_far_z;

vec2 encodeNormal(vec3 normal);

void main()
{
	// simple alpha test, discard fragment if fransparent "enough"
	float alpha = texture(u_albedo_texture, in_texcoord).a;
	if (alpha < 0.5) discard;

	// manual linearized, radial depth
	float dist = distance(in_world_pos, u_cam_pos);  // assuming 'light.position' is available or passed as uniform
	gl_FragDepth = dist / u_far_z;  // [0, 1] depth relative to light range

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
