#include "trigger.h"

namespace RGL::trigger
{

Trigger::Trigger(std::string_view name) :
	_name(name)
{
}

// ----------------------------------------------------------------------------

AABB::AABB(std::string_view name, const bounds::AABB &aabb) :
	Trigger(name),
	_aabb(aabb)
{
}

// ----------------------------------------------------------------------------

bool AABB::triggeredAt(const glm::vec3 &point)
{
	return intersect::check(_aabb, point);
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

Sphere::Sphere(std::string_view name, const bounds::Sphere &sphere) :
	Trigger(name),
	_sphere(sphere)
{
}

// ----------------------------------------------------------------------------

bool Sphere::triggeredAt(const glm::vec3 &point)
{
	return intersect::check(_sphere, point);
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

HalfSpace::HalfSpace(std::string_view name, const Plane &plane) :
	Trigger(name),
	_plane(plane)
{
}

// ----------------------------------------------------------------------------

bool HalfSpace::triggeredAt(const glm::vec3 &point)
{
	return math::facing(_plane, point);
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

} // RGL::trigger