#include "plane.h"
#include "glm/geometric.hpp"
#include <cstdio>

namespace RGL
{


void Plane::set(const glm::vec3 &normal, float offset)
{
	_normal = normal;
	_offset = offset;
}

void Plane::set(const glm::vec4 &normal_and_offset)
{
	set(glm::vec3(normal_and_offset.x, normal_and_offset.y, normal_and_offset.z), normal_and_offset.w);
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
	const auto sqLen = glm::dot(c, c);
	return sqLen < 0.0001f;  // TODO: epsilon
}

bool intersect(const Plane &A, const Plane &B, const Plane &C, glm::vec3 &point)
{
	// if any of the two planes are parallel, there can be no single point of intersection
	if(parallel(A, B) or parallel(A, C) or parallel(B, C))
		return false;

	//       d1 ( N2 * N3 ) + d2 ( N3 * N1 ) + d3 ( N1 * N2 )
	// P =  ---------------------------------------------------
	//                     N1 . ( N2 * N3 )

	const float denominator = glm::dot(A.normal(), glm::cross(B.normal(), C.normal()));
	if(std::abs(denominator) < 0.0001f) // TODO: epsilon
		return false;

	const auto nominator = \
		 A.offset() * glm::cross(B.normal(), C.normal())
		- B.offset() * glm::cross(A.normal(), C.normal())
		+ C.offset() * glm::cross(A.normal(), B.normal());

	point = nominator / -denominator;

	return true;
}

} // math

} // RGL
