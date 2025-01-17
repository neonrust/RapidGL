#version 460 core

layout (location = 0) in vec3 in_pos;

uniform mat4 u_model;

void main()
{
	gl_Position = u_model * vec4(in_pos, 1.0);
}