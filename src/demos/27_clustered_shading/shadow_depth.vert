#version 460

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_texcoord;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec3 out_world_pos;
layout(location = 1) out vec2 out_texcoord;
layout(location = 2) out vec3 out_normal;

uniform mat4 u_mvp;
uniform mat4 u_model;
uniform mat3 u_normal_matrix;

void main()
{
	out_texcoord  = in_texcoord;
	out_world_pos = vec3(u_model * vec4(in_pos, 1));
	out_normal = normalize(u_normal_matrix * in_normal);

	gl_Position = u_mvp * vec4(in_pos, 1);
}
