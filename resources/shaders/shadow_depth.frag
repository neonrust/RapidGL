#version 460

#include "light.glh"

layout(location = 0) in vec3 in_world_pos;
layout(location = 1) in vec2 in_texcoord;

layout(binding = 0) uniform sampler2D u_albedo_texture;

// since we're not explicitly writing depth (but do write color),
// we can, and should, force the early z-rejection test.
layout(early_fragment_tests) in;
void main()
{
	// simple alpha test, discard fragment if fransparent "enough"
	float alpha = texture(u_albedo_texture, in_texcoord).a;
	if(alpha < 0.5)
		discard;
}
