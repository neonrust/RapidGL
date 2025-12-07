#version 460 core

#include "shared-structs.glh"

layout (location = 0) in vec3 in_world_pos;
layout (location = 4) in mat4 in_transform; // locations 4 through 7
layout (location = 8) in vec3 in_color_intensity;
layout (location = 9) in uint in_double_sided;

layout(location = 0) flat out vec3 out_color_intensity;
layout(location = 1) flat out uint out_double_sided;

uniform mat4 u_view_projection;

void main()
{
	out_color_intensity = in_color_intensity;
	out_double_sided = in_double_sided;

	gl_Position = u_view_projection * in_transform * vec4(in_world_pos, 1);
}
