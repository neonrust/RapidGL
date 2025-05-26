// Type definitions shared between C++ and GLSL

#ifdef __cplusplus
// #pragma once
// #define uint  alignas(4)  uint32_t
// #define vec2  alignas(8)  glm::vec2
// #define vec3  alignas(16) glm::vec3
// #define vec4  alignas(16) glm::vec4
// #define uvec3 alignas(16) glm::uvec3
// #define uvec4 alignas(16) glm::uvec4
// #define mat4  alignas(64) glm::mat4
// #define bool  alignas(4)  bool
// #define LightID alignas(4) uint32_t
// #else
// #define LightID uint
#endif

#include "light_constants.h"  // IWYU pragma: keep

#include "ssbo_binds.h"
#include "generated/shared-structs.h"


// 'feature_flags' bits
#define LIGHT_SHADOW_CASTER       0x01

// struct BaseLight
// {
//     vec3 color;
// 	float intensity;
// 	// float fog;
// 	// uint feature_flags;
// 	// LightID uuid;
// };

// struct DirectionalLight
// {
//     BaseLight base;
//     vec3 direction;
// };

// struct PointLight
// {
//     BaseLight base;
//     vec3 position;
//     float radius;

// 	// shadow map
// 	// mat4 view_projection[6];  // can be automatically computed
// 	// uvec4 shadow_tile[6];     // x, y, w, h   for each face
// };

// struct SpotLight
// {
//     PointLight point;
//     vec3 direction;
//     float inner_angle;
//     float outer_angle;
// 	float bounds_radius;  // also the distance from 'point.position' along 'direction'
// };

// struct AreaLight
// {
//     BaseLight base;
//     vec4 points[4];
//     bool two_sided;
// };

// struct AABB
// {
// 	vec4 min;
// 	vec4 max;
// };

// struct ClusterAABB
// {
// 	vec4 min;
// 	vec4 max;
// };

// struct IndexRange
// {
//     uint offset;
//     uint count;
// };


// // almost static stuff (updated when lights change/move)
// struct LightsManagement  // MappedSSBO : SSBO_BIND_LIGHTS
// {
// 	uint num_dir_lights;
// 	uint num_point_lights;
// 	uint num_spot_lights;
// 	uint num_area_lights;
// 	DirectionalLight dir_lights[1];  // 0..1
// 	PointLight point_lights[MAX_POINT_LIGHTS];
// 	SpotLight spot_lights[MAX_SPOT_LIGHTS];
// 	AreaLight area_lights[MAX_AREA_LIGHTS];
// };

// discovery of non-empty clusters
// struct ClusterDiscovery  // regular SSBO : SSBO_BIND_CLUSTER_DISCOVERY
// {
// 	uvec3 cull_lights_args;         // placed first to simplify indirect dispatch
// 	uint active_clusters[CLUSTER_MAX_COUNT];      // num_active  'active' ?
// 	uint num_active;
// 	bool nonempty_clusters[CLUSTER_MAX_COUNT];    // 'flagged'/'marked'/'discovered' ?
// };
// uints: 4 + (1 + 1)*num_clusters + 1 = ~164 kB @ 20k

// light culling
// struct LightCulling // regular SSBO : SSBO_BIND_CLUSTER_LIGHTS
// {
// 	IndexRange cluster_area_lights[CLUSTER_MAX_COUNT];  // num_clusters
// 	IndexRange cluster_point_lights[CLUSTER_MAX_COUNT]; // num_clusters
// 	IndexRange cluster_spot_lights[CLUSTER_MAX_COUNT];  // num_clusters

// 	uint global_point_light_counter;
// 	uint global_spot_light_counter;
// 	uint global_area_light_counter;

// 	uint point_light_index_list[CLUSTER_AVERAGE_LIGHTS * CLUSTER_MAX_COUNT];
// 	uint spot_light_index_list [CLUSTER_AVERAGE_LIGHTS * CLUSTER_MAX_COUNT];
// 	uint area_light_index_list [CLUSTER_AVERAGE_LIGHTS * CLUSTER_MAX_COUNT];
// };
// uints: (2 + 2 + 2 + 50 + 50 + 50)*num_clusters + 3 = ~16 Mb @ 20k


// struct ShadowMapParams
// {
// 	mat4 view_proj[6];
// 	uvec4 atlas_rect[6];
// };


// #ifdef __cplusplus
// // remove the macros we defined at the top
// #undef vec2
// #undef vec3
// #undef vec4
// #undef uvec3
// #undef uvec4
// #undef mat4
// #undef uint
// #undef bool
// #undef LightID
// #endif
