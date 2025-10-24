#pragma once


#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

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
	glm::vec3 position  { 0, 0, 0 }  \

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
	float inner_angle     { 0 };        \
	float bounds_radius  // also the distance from 'position' along 'direction'

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

#define AREA_LIGHT        \
	COMMON;               \
	glm::vec3 points[4];  \
	bool two_sided

struct AreaLightParams
{
	AREA_LIGHT;
};

struct AreaLight
{
	AREA_LIGHT;
	INTERNAL;
};

// ------------------------------------------------------------------

#define TUBE_LIGHT           \
	COMMON;                  \
	glm::vec3 end_points[2];  /* stored in GPULight shape_points[0-1] */ \
	float thickness      // stored in GPULight shape_points[2]

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
	float sphere_radius // stored in GPULight::shape_points[0]

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
	float disc_radius  // stored in GPULight::shape_points[0]

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
#undef AREA_LIGHT
#undef SPHERE_LIGHT
#undef DISC_LIGHT
