#pragma once


#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/ext/quaternion_float.hpp>

#include <cstdint>

using LightID = uint32_t;  // entity ID
static constexpr LightID NO_LIGHT_ID { std::numeric_limits<LightID>::max() };

using LightIndex = uint32_t;
static constexpr LightID NO_LIGHT_INDEX { std::numeric_limits<LightIndex>::max() };

class LightManager;

// ------------------------------------------------------------------

// properties common to ALL lights
#define COMMON                              \
	glm::vec3 color     { 1.f, 1.f, 1.f };  \
	float intensity     { 10.f };           \
	float fog           { 0.f };            \
	bool shadow_caster  { false }

#define INTERNAL                                \
	inline LightID id() const { return uuid; }  \
private:                                        \
	friend class LightManager;                  \
	LightID uuid        { NO_LIGHT_ID }

#define POINT                        \
	glm::vec3 position  { 0, 0, 0 }

#define SURFACE           \
	bool visible_surface

// ------------------------------------------------------------------

#define POINT_LIGHT  \
	COMMON;          \
	POINT

struct PointLightParams
{
	POINT_LIGHT;
};

struct PointLight
{
	POINT_LIGHT;
	INTERNAL;
};

// ------------------------------------------------------------------

#define DIRECTION  \
	glm::vec3 direction { 0, 0, -1 }

#define DIR_LIGHT  \
	COMMON;        \
	DIRECTION

struct DirectionalLightParams
{
	DIR_LIGHT;
};

struct DirectionalLight
{
	DIR_LIGHT;
	INTERNAL;
};

// ------------------------------------------------------------------

#define SPOT_LIGHT  \
	COMMON;         \
	POINT;          \
	DIRECTION; \
	float outer_angle     { glm::radians(15.f) }; \
	float inner_angle     { 0 }

// '(outer|inner)_angle is in radians

struct SpotLightParams
{
	SPOT_LIGHT;
};

struct SpotLight
{
	SPOT_LIGHT;
	INTERNAL;
};

// ------------------------------------------------------------------

#define RECT_LIGHT                      \
	COMMON;                             \
	POINT;                              \
	glm::vec2 size        { 1, 1 };     \
	glm::quat orientation;              \
	bool double_sided { false };        \
	SURFACE


struct RectLightParams
{
	RECT_LIGHT;
};

struct RectLight
{
	RECT_LIGHT;
	INTERNAL;
};

// ------------------------------------------------------------------

#define TUBE_LIGHT       \
	COMMON;              \
	POINT;               \
	glm::vec3 end_points[2];  /* relative 'position' stored in GPULight shape_points[0-1] */ \
	float thickness;          /* stored in GPULight shape_points[2].x */ \
	SURFACE

struct TubeLightParams
{
	TUBE_LIGHT;
};

struct TubeLight
{
	TUBE_LIGHT;
	INTERNAL;
};

// ------------------------------------------------------------------

#define SPHERE_LIGHT      \
	COMMON;               \
	POINT;                \
	float radius;         \
	SURFACE
// 'radius' stored in GPULight::shape_points[0].x

struct SphereLightParams
{
	SPHERE_LIGHT;
};

struct SphereLight
{
	SPHERE_LIGHT;
	INTERNAL;
};

// ------------------------------------------------------------------

#define DISC_LIGHT       \
	COMMON;              \
	POINT;               \
	glm::vec3 direction; \
	float radius;        \
	bool double_sided { false }; \
	SURFACE
// 'radius' stored in GPULight::shape_points[0].x

struct DiscLightParams
{
	DISC_LIGHT;
};

struct DiscLight
{
	DISC_LIGHT;
	INTERNAL;
};

// ------------------------------------------------------------------


#undef COMMON
#undef POINT
#undef INTERNAL

#undef POINT_LIGHT
#undef DIR_LIGHT
#undef SPOT_LIGHT
#undef RECT_LIGHT
#undef SPHERE_LIGHT
#undef DISC_LIGHT
