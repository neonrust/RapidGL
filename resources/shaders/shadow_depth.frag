#version 460

#include "light.glh"

layout(location = 0) in vec3 in_world_pos;
layout(location = 1) in vec2 in_texcoord;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec2 out_normal; // color attachment 0

layout(binding = 0) uniform sampler2D u_albedo_texture;

uniform uint u_shadow_slot_index;

vec2 encodeNormal(vec3 normal);
float computeFragmentBias(float normalized_depth, vec3 light_to_frag, vec3 depth_normal);

// since we're not explicitly writing depth (but do write color),
// we can, and should, force the early z-rejection test.
layout(early_fragment_tests) in;
void main()
{
	// simple alpha test, discard fragment if fransparent "enough"
	float alpha = texture(u_albedo_texture, in_texcoord).a;
	if(alpha < 0.5)
		discard;

	// also write normals
    out_normal = encodeNormal(in_normal);
}

vec2 encodeNormal(vec3 n)
{
	// octahedral encoding
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 enc = n.xy;
    if (n.z < 0)
        enc = (1 - abs(enc.yx)) * sign(enc.xy);
    return enc * 0.5 + 0.5; // Map from [-1,1] to [0,1]
}
