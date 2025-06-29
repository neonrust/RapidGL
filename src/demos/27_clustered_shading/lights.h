#pragma once


#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

using LightID = uint32_t;  // entity ID
static constexpr LightID NO_LIGHT_ID { std::numeric_limits<LightID>::max() };

class LightManager;

// properties common to ALL lights
#define COMMON \
	glm::vec3 color     { 1.f, 1.f, 1.f }; \
	float intensity     { 10.f }; \
	float fog           { 0.f }; \
	bool shadow_caster  { false }

#define INTERNAL \
	inline LightID id() const { return uuid; } \
private: \
	friend class LightManager; \
	LightID uuid        { NO_LIGHT_ID }; \
	uint32_t list_index { std::numeric_limits<uint32_t>::max() }

#define POINT \
	glm::vec3 position  { 0, 0, 0 }; \
	float affect_radius        { 3.5f }

struct DirectionalLight
{
	COMMON;
	glm::vec3 direction { 0, 0, -1 };
	INTERNAL;
};

struct PointLight
{
	COMMON;
	POINT;
	INTERNAL;
};

struct PointLightDef
{
	COMMON;
	POINT;
};

struct SpotLight
{
	COMMON;
	POINT;
	glm::vec3 direction   { 0, 0, -1 };
	float inner_angle     { 0 };
	float outer_angle     { glm::radians(15.f) };
	float bounds_radius;  // also the distance from 'position' along 'direction'
	INTERNAL;
};

struct AreaLight
{
	COMMON;
	glm::vec4 points[4];
	bool two_sided;
	INTERNAL;
};

struct TubeLight
{
	COMMON;
	glm::vec4 points[2];  // stored in GPULight shape_points[0-1]
	float thickness;      // stored in GPULight shape_points[2]
	INTERNAL;
};

struct SphereLight
{
	COMMON;
	POINT;
	float sphere_radius; // stored in GPULight::shape_points[0]
	INTERNAL;
};

struct DiscLight
{
	COMMON;
	glm::vec3 position;
	float disc_radius;   // stored in GPULight::shape_points[0]
	glm::vec3 direction;
	INTERNAL;
};


#undef COMMON
#undef POINT
#undef INTERNAL
