#pragma once

#include <numbers>

namespace RGL::component
{

constexpr float deg2rad(float d)
{
	return d * std::numbers::pi_v<float> / 180.f;
}

struct CameraPerspective
{
	inline void set_fov(float vertical) {
		vertical_fov = deg2rad(vertical);
		frustum_dirty = true;
	}

	float vertical_fov  { 60.f };   // in degrees
	float focal_length  { 1.f };
	float near          { 0.1f };
	float far           { 200.f };

	bool frustum_dirty { true };
};

} // RGL::component