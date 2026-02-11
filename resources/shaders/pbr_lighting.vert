#version 460 core
layout (location = 0) in vec3 in_pos;
layout (location = 1) in vec2 in_texcoord;
layout (location = 2) in vec3 in_normal;

const uint MAX_CASCADES = 4;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_mvp;
uniform mat3 u_normal_matrix;
uniform uint u_csm_num_cascades;

uniform mat4  u_csm_light_view_space[MAX_CASCADES];
uniform mat4  u_csm_light_clip_space[MAX_CASCADES];

layout (location = 0) out vec2 out_texcoord;
layout (location = 1) out vec3 out_world_pos;
layout (location = 2) out vec3 out_view_pos;
layout (location = 3) out vec4 out_clip_pos;
layout (location = 4) out vec3 out_normal;

layout (location = 6)  out vec3 out_csm_light_view_pos[MAX_CASCADES];
layout (location = 10) out vec3 out_csm_light_uv_pos[MAX_CASCADES];

void main()
{
	out_world_pos = (u_model * vec4(in_pos, 1)).xyz;
	out_view_pos  = (u_view * vec4(out_world_pos, 1)).xyz;//u_model * vec4(in_pos, 1));
	out_clip_pos  = u_mvp * vec4(in_pos, 1);
	out_texcoord  = in_texcoord;
	out_normal    = u_normal_matrix * in_normal;

	gl_Position = out_clip_pos;

	for(uint cascade = 0; cascade < u_csm_num_cascades; ++cascade)
	{
		out_csm_light_view_pos[cascade] = (u_csm_light_view_space[cascade] * vec4(out_world_pos, 1)).xyz;

		vec3 clip_pos = (u_csm_light_clip_space[cascade] * vec4(out_world_pos, 1)).xyz; // [-1, 1]
		vec3 ndc_pos = clip_pos.xyz; // b/c .w = 1
		vec3 uv_pos = ndc_pos * 0.5 + 0.5; // [0, 1]
		out_csm_light_uv_pos[cascade] = uv_pos;
	}

}
