#pragma once

#include "light_constants.h"
#include <cstdint>

namespace RGL
{

enum class LightType : uint_fast8_t
{
	Point       = LIGHT_TYPE_POINT,
	Directional = LIGHT_TYPE_DIRECTIONAL,
	Spot        = LIGHT_TYPE_SPOT,
	Rect        = LIGHT_TYPE_RECT,
	Tube        = LIGHT_TYPE_TUBE,
	Sphere      = LIGHT_TYPE_SPHERE,
	Disc        = LIGHT_TYPE_DISC,
};
static_assert(LIGHT_TYPE__COUNT == 7);

} // RGL