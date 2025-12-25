#version 460 core
layout (location = 0) in vec3 in_pos;
layout (location = 1) in vec2 in_texcoord;
layout (location = 2) in vec3 in_normal;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_mvp;
uniform mat3 u_normal_matrix;

uniform uint u_csm_num_cascades;  // 0 - 4  (0 = disabled, duh)
// uniform mat4 u_csm_light_view[4];
uniform mat4 u_csm_light_view_proj[4];

layout (location = 0) out vec2 out_texcoord;
layout (location = 1) out vec3 out_world_pos;
layout (location = 2) out vec3 out_view_pos;
layout (location = 3) out vec4 out_clip_pos;
layout (location = 4) out vec3 out_normal;

// layout (location = 6) out vec4 out_light_view_space_pos[4];
layout (location = 7) out vec4 out_light_clip_space_pos[4];

void main()
{
    out_world_pos = vec3(u_model * vec4(in_pos,          1));
	out_view_pos  = vec3(u_view * u_model * vec4(in_pos, 1));
	out_clip_pos  = u_mvp * vec4(in_pos, 1);
    out_texcoord  = in_texcoord;
    out_normal    = u_normal_matrix * in_normal;

    for(uint idx = 0; idx < u_csm_num_cascades; ++idx)
    {
    	// view proj already stored in slot info?
     	// except that this is probably faster (per vertex vs per pixel)
      	// however, this is done for *all* vertices
        // out_light_view_space_pos[idx] = u_csm_light_view[idx] * vec4(out_world_pos, 1);
        out_light_clip_space_pos[idx] = u_csm_light_view_proj[idx] * vec4(out_world_pos, 1);
    }

    gl_Position = out_clip_pos;
}
