// Type definitions shared between C++ and GLSL

#ifdef __cplusplus
#pragma once
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#define vec3  alignas(16) glm::vec3
#define vec4  alignas(16) glm::vec4
#define uvec3 alignas(16) glm::uvec3
#define uint  alignas(4)  uint32_t
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"   // it's a necessary evil
#define bool alignas(4)  bool
#pragma clang diagnostic pop
#endif

#define SSBO_BIND_CLUSTERS                    1
#define SSBO_BIND_DIRECTIONAL_LIGHTS          2
#define SSBO_BIND_POINT_LIGHTS                3
#define SSBO_BIND_SPOT_LIGHTS                 4
#define SSBO_BIND_AREA_LIGHTS                 5

struct BaseLight
{
    vec3 color;
	float intensity;
	// TODO: float fog; // [ 0, 1 ]
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

#define CLUSTER_MAX_POINT_LIGHTS 256
#define CLUSTER_MAX_SPOT_LIGHTS 32
#define CLUSTER_MAX_AREA_LIGHTS 8

struct Cluster
{
	AABB aabb;

	uint num_point_lights;   // if > CLUSTER_MAX_POINT_LIGHTS, too many lights affect the cluster
	uint point_lights[CLUSTER_MAX_POINT_LIGHTS];

	uint num_spot_lights;    // if > CLUSTER_MAX_SPOT_LIGHTS, too many lights affect the cluster
	uint spot_lights[CLUSTER_MAX_SPOT_LIGHTS];

	uint num_area_lights;    // if > CLUSTER_MAX_AREA_LIGHTS, too many lights affect the cluster
	uint area_lights[CLUSTER_MAX_AREA_LIGHTS];

	uvec3 coord;
	bool visited;
};

struct LightGrid
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
#undef bool
#endif
