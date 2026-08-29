#include "room_portal.h"

#include "frustum.h"
#include "bounds.h"

#include <cmath>
#include <glm/geometric.hpp>

namespace RGL
{

PortalGeometry::PortalGeometry(const glm::vec3 &center, const glm::vec3 &direction, const glm::vec2 &halfExtent) :
	_center(center),
	_direction(direction),
	_halfExtent(halfExtent)
{
	_radius = std::sqrt(_halfExtent.x*_halfExtent.x + _halfExtent.y*_halfExtent.y);
}

RoomPortal::RoomPortal(const PortalGeometry &geom) :
	_geometry(geom)
{
}

namespace intersect
{

bool check(const Frustum &f, const PortalGeometry &geom)
{
	if(glm::dot(f.forward(), geom.direction()) <= 0)
		return false;

	// approximate as sphere
	//. TODO: for thin/wide portals, it might be necessary to do a more precise test
	const bounds::Sphere sphere{ geom.center(), geom.radius() };
	return check(f, sphere);
}

} // intersect


namespace math
{

Frustum narrow(const Frustum &f, const PortalGeometry &geom)
{
	// approximate the portal as a sphere, same as intersect::check() above
	const bounds::Sphere sphere{ geom.center(), geom.radius() };

	Frustum result = f;
	result.narrowToSphere(sphere);
	return result;
}

} // math

} // RGL