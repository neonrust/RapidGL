// Type definitions shared between C++ and GLSL

#ifdef __cplusplus
#pragma once
#define vec3  alignas(16) glm::vec3
#define vec4  alignas(16) glm::vec4
#define uvec3 alignas(16) glm::uvec3
#define uint  alignas(4)  uint32_t
#define LightID alignas(4) uint32_t
#else
#define LightID uint
#endif

#define SSBO_BIND_CLUSTERS_AABB               1
#define SSBO_BIND_POINT_LIGHT_INDEX           2
#define SSBO_BIND_SPOT_LIGHT_INDEX            3
#define SSBO_BIND_AREA_LIGHT_INDEX            4
#define SSBO_BIND_CLUSTER_POINT_LIGHTS        5
#define SSBO_BIND_CLUSTER_SPOT_LIGHTS         6
#define SSBO_BIND_CLUSTER_AREA_LIGHTS         7
#define SSBO_BIND_NONEMPTY_CLUSTERS           8
#define SSBO_BIND_ACTIVE_CLUSTERS             9
// #define SSBO_BIND_GLOBAL_LIGHT_COUNTERS       10

#define SSBO_BIND_DIRECTIONAL_LIGHTS          11
#define SSBO_BIND_POINT_LIGHTS                12
#define SSBO_BIND_SPOT_LIGHTS                 13
#define SSBO_BIND_AREA_LIGHTS                 14
#define SSBO_BIND_CULL_DISPATCH_ARGS          15

// 'feature_flags' bits
#define LIGHT_SHADOW_CASTER       0x01

struct BaseLight
{
	LightID uuid;
    vec3 color;
	float intensity;
	float fog;
	uint feature_flags;
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

#define CLUSTER_MAX_POINT_LIGHTS   256
#define CLUSTER_MAX_SPOT_LIGHTS     32
#define CLUSTER_MAX_AREA_LIGHTS      8
#define CLUSTER_INDEX_MAX           9999999

// struct Cluster
// {
// 	AABB aabb;

// 	uint num_point_lights;   // if > CLUSTER_MAX_POINT_LIGHTS, too many lights affect the cluster
// 	uint point_lights[CLUSTER_MAX_POINT_LIGHTS];

// 	uint num_spot_lights;    // if > CLUSTER_MAX_SPOT_LIGHTS, too many lights affect the cluster
// 	uint spot_lights[CLUSTER_MAX_SPOT_LIGHTS];

// 	uint num_area_lights;    // if > CLUSTER_MAX_AREA_LIGHTS, too many lights affect the cluster
// 	uint area_lights[CLUSTER_MAX_AREA_LIGHTS];
// };

// #define NONEMPTY_CLUSTERS_END     9999999

// struct ClusterNonempty
// {
// 	uint cluster_index;
// 	bool found;
// };

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

#ifdef __cplusplus
// remove the macros we defined at the top
#undef vec3
#undef vec4
#undef uvec3
#undef uint
#endif
