#pragma once

#include "bounds.h"
// #include "plane.h"
#include "variable.h"

#include <string>

namespace RGL::component
{

enum class TriggerShape
{
	AABB,
	Sphere,
	Cylinder,
	// HalfSpace
};

struct TriggerGeneral
{
	inline TriggerGeneral(TriggerShape s) : shape(s) {};

	TriggerShape shape;

	std::string name;
	bool enabled { true };
	uint8_t trigger_limit { 0 }; // 0 = no limit
	uint8_t ignore_count  { 0 };
	glm::vec3 facing_direction { 0 };
	float direction_tolerance { 45.f };

	uint8_t triggered_count { 0 };
};

// preferrably, these should not have a spatial location or orientation,
//   since that should be handled by a Transform component
using TriggerShapeAABB = RGL::bounds::AABB;
using TriggerShapeSphere = RGL::bounds::Sphere;
using TriggerShapeCylinder = RGL::bounds::Cylinder;
// using TriggerShapeHalfSpace = RGL::Plane;

struct VariableSetting
{
	std::string name;
	VariableValue value;
};

using VariablePack = std::vector<VariableSetting>;

} // RGL::component