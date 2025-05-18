// Type definitions shared between C++ and GLSL

#ifdef __cplusplus
#pragma once
#define uint  alignas(4)  uint32_t
#define vec2  alignas(8)  glm::vec2
#define vec3  alignas(16) glm::vec3
#define vec4  alignas(16) glm::vec4
#define uvec3 alignas(16) glm::uvec3
#define uvec4 alignas(16) glm::uvec4
#define mat3  alignas(48) glm::mat3
#define mat4  alignas(64) glm::mat4
#define LightID alignas(4) uint32_t
#else
#define LightID uint
#endif

#define MAX_POINT_LIGHTS              2048
#define MAX_SPOT_LIGHTS                256
#define MAX_AREA_LIGHTS                 64
#define MAX_POINT_SHADOW_CASTERS       256
#define MAX_SPOT_SHADOW_CASTERS         32
#define MAX_AREA_SHADOW_CASTERS          2

#define CLUSTER_MAX_COUNT           131072
#define CLUSTER_MAX_POINT_LIGHTS       256
#define CLUSTER_MAX_SPOT_LIGHTS         32
#define CLUSTER_MAX_AREA_LIGHTS          8
#define CLUSTER_AVERAGE_LIGHTS          64
#define CLUSTER_INDEX_MAX          9999999

#define SSBO_BIND_CLUSTERS_AABB               1
#define SSBO_BIND_LIGHTS                 10
// #define SSBO_BIND_CLUSTERS               11
// #define SSBO_BIND_NONEMPRY_CLUSTERS      12
// #define SSBO_BIND_CLUSTER_LIGHTS         13
#define SSBO_BIND_POINT_LIGHT_INDEX           2
#define SSBO_BIND_SPOT_LIGHT_INDEX            3
#define SSBO_BIND_AREA_LIGHT_INDEX            4
#define SSBO_BIND_CLUSTER_POINT_LIGHTS        5
#define SSBO_BIND_CLUSTER_SPOT_LIGHTS         6
#define SSBO_BIND_CLUSTER_AREA_LIGHTS         7
#define SSBO_BIND_NONEMPTY_CLUSTERS           8
#define SSBO_BIND_ACTIVE_CLUSTERS             9

#define SSBO_BIND_CULL_DISPATCH_ARGS          15

// 'feature_flags' bits
#define LIGHT_SHADOW_CASTER       0x01

struct BaseLight
{
    vec3 color;
	float intensity;
	// float fog;
	// uint feature_flags;
	// LightID uuid;
};

struct DirectionalLight
{
    BaseLight base;
    vec3 direction;
};

struct PointLight
{
    BaseLight base;
    vec3 position;
    float radius;

	// shadow map
	// mat4 view_projection[6];  // can be automatically computed
	// uvec4 shadow_tile[6];     // x, y, w, h   for each face
};

struct SpotLight
{
    PointLight point;
    vec3 direction;
    float inner_angle;
    float outer_angle;
	float bounds_radius;  // also the distance from 'point.position' along 'direction'
};

struct AreaLight
{
    BaseLight base;
    vec4 points[4];
    bool two_sided;
};

struct AABB
{
	vec4 min;
	vec4 max;
};

struct ClusterAABB
{
	vec4 min;
	vec4 max;
};

struct IndexRange
{
    uint offset;
    uint count;
};


// almost static stuff (updated when lights change/move)
struct LightsManagement  // MappedSSBO : SSBO_BIND_LIGHTS
{
	uint num_dir_lights;
	uint num_point_lights;
	uint num_spot_lights;
	uint num_area_lights;
	DirectionalLight dir_lights[1];  // 0..1
	PointLight point_lights[MAX_POINT_LIGHTS];
	SpotLight spot_lights[MAX_SPOT_LIGHTS];
	AreaLight area_lights[MAX_AREA_LIGHTS];

#ifdef __cplusplus
	// hm, can't use ranged for with these anyway... :(
	inline DirectionalLight *dir_begin() { return dir_lights; }
	inline DirectionalLight *dir_end() { return dir_lights + num_dir_lights; }
	inline PointLight *points_begin() { return point_lights; }
	inline PointLight *points_end() { return point_lights + num_point_lights; }
	inline SpotLight *spot_begin() { return spot_lights; }
	inline SpotLight *spot_end() { return spot_lights + num_spot_lights; }
	inline AreaLight *area_begin() { return area_lights; }
	inline AreaLight *area_end() { return area_lights + num_area_lights; }
#endif
};

// could first try these four SSBOs, then perhaps merge everything,
//   into a single persistently memory mapped SSBO
// static info (updated once)
struct ClusterManagement  // regular SSBO : SSBO_BIND_CLUSTERS
{
	uvec3 cluster_resolution;
	uint num_clusters;
	ClusterAABB clusters_aabb[CLUSTER_MAX_COUNT]; // num_clusters
};

// discovery of non-empty clusters
struct ClustersDiscovery  // regular SSBO : SSBO_BIND_NONEMPRY_CLUSTERS
{
	bool nonempty_clusters[CLUSTER_MAX_COUNT];    // num_clusters  'flagged' ?
	uint active_clusters[CLUSTER_MAX_COUNT];      // num_clusters  'active' ?
	uint global_nonempty_count;                   // 'global_count' /
	uvec3 cull_lights_args;
};

// light culling
struct LightCulling // regular SSBO : SSBO_BIND_CLUSTER_LIGHTS
{
	IndexRange cluster_area_lights[CLUSTER_MAX_COUNT];  // num_clusters
	IndexRange cluster_point_lights[CLUSTER_MAX_COUNT]; // num_clusters
	IndexRange cluster_spot_lights[CLUSTER_MAX_COUNT];  // num_clusters

	uint global_point_light_counter;
	uint global_spot_light_counter;
	uint global_area_light_counter;

	uint point_light_index_list[CLUSTER_AVERAGE_LIGHTS * CLUSTER_MAX_COUNT];
	uint spot_light_index_list [CLUSTER_AVERAGE_LIGHTS * CLUSTER_MAX_COUNT];
	uint area_light_index_list [CLUSTER_AVERAGE_LIGHTS * CLUSTER_MAX_COUNT];
};


// struct ShadowMapParams
// {
// 	mat4 view_proj[6];
// 	uvec4 atlas_rect[6];
// };


#ifdef __cplusplus
// remove the macros we defined at the top
#undef vec2
#undef vec3
#undef vec4
#undef uvec3
#undef uvec4
#undef mat3
#undef mat4
#undef uint
#undef LightID
#endif
