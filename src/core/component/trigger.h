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
	TriggerShape shape;
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