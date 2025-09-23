#version 460 core

layout (location = 0) in vec2 in_pos;
layout (location = 1) in vec2 in_texcoord;

uniform mat4 u_projection;

layout (location = 0) out vec2 out_texcoord;

void main()
{
	out_texcoord = in_texcoord;
	gl_Position = u_projection * vec4(in_pos.xy, 0, 1);
}
