#pragma once


#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

using LightID = uint32_t;  // entity ID
static constexpr LightID NO_LIGHT_ID { LightID(-1) };

class LightManager;

#define BASE \
	glm::vec3 color     { 1.f, 1.f, 1.f }; \
	float intensity     { 10.f }; \
	float fog           { 0.f }; \
	bool shadow_caster  { false }

#define INTERNAL \
	inline LightID id() const { return uuid; } \
private: \
	friend class LightManager; \
	LightID uuid        { LightID(-1) }; \
	uint32_t list_index { uint32_t(-1) }

#define POINT \
	glm::vec3 position  { 0, 0, 0 }; \
	float radius        { 3.5f }

struct DirectionalLight
{
	BASE;
	glm::vec3 direction { 0, 0, -1 };
	INTERNAL;
};

struct PointLight
{
	BASE;
	POINT;
	INTERNAL;
};

struct PointLightDef
{
	BASE;
	POINT;
};

struct SpotLight
{
	BASE;
	POINT;
	glm::vec3 direction   { 0, 0, -1 };
	float inner_angle     { 0 };
	float outer_angle     { glm::radians(30.f) };
	float bounds_radius;  // also the distance from 'position' along 'direction'
	INTERNAL;
};

struct AreaLight
{
	BASE;
	glm::vec4 points[4];
	bool two_sided;
	INTERNAL;
};


#undef BASE
#undef POINT
#undef INTERNAL
