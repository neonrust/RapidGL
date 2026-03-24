#pragma once

#include <glm/vec3.hpp>

namespace RGL
{

enum class LightType : uint_fast8_t;

namespace component
{

struct LightGeneral
{
	LightType     light_type;    // LIGHT_TYPE_*
	bool          enabled;
	glm::vec3     color;         // { 0, 0, 0 } to { 1, 1, 1 }
	float         intensity;     // >= 0
	bool          surface;
	bool          volumetric;
	float         fog;           // >= 0
	bool          cast_shadow;
	uint_fast16_t shadow_index;
};

} // component

} // RGL