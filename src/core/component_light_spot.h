#pragma once

namespace RGL
{

namespace component
{

struct SpotLight
{
	// half-angles, in radians
	float outer_angle  { 0.5f };  // >= 0
	float inner_angle  { 0.4f };  // 0 to outer_angle
};

} // component

} // RGL