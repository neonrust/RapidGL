#include "plane.h"
#include "glm/geometric.hpp"

namespace RGL
{


void Plane::set(const glm::vec3 &normal, float offset)
{
	_normal = normal;
	_offset = offset;
}

namespace math
{

float distance(const Plane &plane, const glm::vec3 &point)
{
	return glm::dot(plane.normal(), point) + plane.offset();
}

bool parallel(const Plane &plane1, const Plane &plane2)
{
	// TODO: is there a faster way?
	const auto c = glm::cross(plane1.normal(), plane2.normal());
	return glm::dot(c, c) < 0.0001f;  // TODO: epsilon
}

} // math

namespace intersect
{

bool check(const RGL::Plane &a, const RGL::Plane &b, const RGL::Plane &c, glm::vec3 *point)
{
	// http://local.wasp.uwa.edu.au/~pbourke/geometry/3planes/

	// if any of the two planes are parallel, there can be no single point of intersection
	if(RGL::math::parallel(a, b) or RGL::math::parallel(a, c) or RGL::math::parallel(b, c))
		return false;

	//       d1 ( N2 * N3 ) + d2 ( N3 * N1 ) + d3 ( N1 * N2 )
	// P =  ---------------------------------------------------
	//                     N1 . ( N2 * N3 )

	const float denominator = glm::dot(a.normal(), glm::cross(b.normal(), c.normal()));
	if(denominator < 0.0001f) // TODO: epsilon
		return false;

	const auto nominator = - a.offset() * glm::cross(b.normal(), c.normal())
		- b.offset() * glm::cross(c.normal(), a.normal())
		- b.offset() * glm::cross(a.normal(), b.normal());

	if(point)
		*point = nominator / denominator;

	return true;
}

} // intersect

} // RGL
