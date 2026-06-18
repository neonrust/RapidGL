#pragma once

#include <cstdint>
#include "frustum.h"

namespace RGL
{

enum class CameraType : uint_fast8_t
{
	Perspective,
	Ortho
};

namespace component
{

struct CameraGeneral
{
	CameraType camera_type { CameraType::Perspective };

	uint32_t width { 1920 };
	uint32_t height { 1080 };
	float exposure { 1.f };

	[[nodiscard]] inline float aspect() const { return float(width) / float(height); }
};

using CameraFrustum = Frustum;

} // component

} // RGL