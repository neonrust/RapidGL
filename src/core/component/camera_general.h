#pragma once

#include <cstdint>
#include "frustum.h"

#include <glm/vec2.hpp>

namespace RGL
{

enum class CameraProjection : uint_fast8_t
{
	Perspective,
	Ortho
};

namespace component
{

struct CameraGeneral
{
	CameraProjection projection_type { CameraProjection::Perspective };

	glm::uvec2 view_size { 1920, 1080 };
	float brightness { 1.f };

	[[nodiscard]] inline float aspect() const { return float(view_size.x) / float(view_size.y); }
};

using CameraFrustum = Frustum;

} // component

} // RGL