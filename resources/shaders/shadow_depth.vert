#version 460
#include "shared-structs.glh"

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_texcoord;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec3 out_world_pos;
layout(location = 1) out vec2 out_texcoord;
layout(location = 2) out vec3 out_normal;

layout(location = 10) flat out float out_near_z;
layout(location = 11) flat out float out_far_z;

uniform mat4 u_model;
uniform mat3 u_normal_matrix;

uniform uint u_shadow_slot_index;
uniform uint u_shadow_map_index;

layout(std430, binding = SSBO_BIND_SHADOW_SLOTS_INFO) readonly buffer ShadowSlotsInfoSSBO
{
	ShadowSlotInfo ssbo_shadow_slots[];
};

void main()
{
	out_texcoord  = in_texcoord;
	out_world_pos = vec3(u_model * vec4(in_pos, 1));
	out_normal = normalize(u_normal_matrix * in_normal);

	ShadowSlotInfo slot_info = ssbo_shadow_slots[u_shadow_slot_index];
	mat4 vp = slot_info.view_proj[u_shadow_map_index];
	mat4 mvp = vp * u_model;

	out_near_z = slot_info.near_plane[u_shadow_map_index];
	out_far_z = slot_info.far_plane[u_shadow_map_index];

	gl_Position = mvp * vec4(in_pos, 1);
}
