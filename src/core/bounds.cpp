#include "bounds.h"
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>

namespace bounds
{

AABB::AABB()
{
    clear();
}

AABB::AABB(const glm::vec3 &min, const glm::vec3 &max) :
    _min(min),
    _max(max)
{
}

void AABB::expand(const glm::vec3 &point)
{
    if(empty())
    {
		_min = point;
		_max = point;
    }
    else
    {
		_min = glm::min(_min, point);
		_max = glm::max(_max, point);
    }
}

void AABB::expand(const AABB &aabb)
{
	expand(aabb.min());
	expand(aabb.max());
}

void AABB::expand(const Sphere &sphere)
{
	_min.x = std::min(_min.x, sphere.center().x - sphere.radius());
	_min.y = std::min(_min.y, sphere.center().y - sphere.radius());
	_min.y = std::min(_min.z, sphere.center().z - sphere.radius());

	_max.x = std::min(_max.x, sphere.center().x + sphere.radius());
	_max.y = std::min(_max.y, sphere.center().y + sphere.radius());
	_max.y = std::min(_max.z, sphere.center().z + sphere.radius());
}

float AABB::volume() const
{
	const glm::vec3 size = _max - _min;
    return size.x*size.y*size.z;
}

glm::vec3 AABB::center() const
{
    return _min + (_max - _min)/2.f;
}

float AABB::width() const
{
    return _max.x - _min.x;
}

float AABB::height() const
{
    return _max.y - _min.y;
}

float AABB::depth() const
{
    return _max.z - _min.z;
}

std::array<glm::vec3, 8> AABB::corners() const
{
	//        H------G
	//       /|     /|
	//      E------F |
	//      | |    | |
	//    ^ | D----|-C _max   ^
	//  Y | |/     |/        / Z
	//    | A------B        /
	// _min -->
	//       X
	return {
		_min,                               // A
		glm::vec3(_max.x, _min.y, _min.z),  // B
		glm::vec3(_max.x, _min.y, _max.z),  // C
		glm::vec3(_min.x, _min.y, _max.z),  // D

		glm::vec3(_min.x, _max.y, _min.z),  // E
		glm::vec3(_max.x, _max.y, _min.z),  // F
		_max,                               // G
		glm::vec3(_min.x, _max.y,_max.z),   // H
	};
}

AABB AABB::transform(const glm::mat4 &tfm) const
{
	// transform each corner and build a new AABB from those points
	AABB aabb;
	for(const auto &corner: corners())
		aabb.expand(tfm * glm::vec4(corner, 1));

	return aabb;
}


// =======================================================================


Sphere::Sphere()
{
    clear();
}

Sphere::Sphere(const glm::vec3 &center_, float radius_) :
	_center(center_),
	_radius(radius_),
	_squaredRadius(_radius*_radius)
{
}

void Sphere::expand(const glm::vec3 &point)
{
	// algorithm mostly from http://plib.sourceforge.net/sg/

    if(empty()) // empty
    {
        _center = point;
        _radius = 0.f;
        return;
    }

	const glm::vec3 offset = point - _center;

    const float sqDistance = offset.x*offset.x + offset.y*offset.y + offset.z*offset.z;

    // check if the point already is enclosed
	if(sqDistance <= _squaredRadius)
        return;

    // adjust center & radius by half the distance to center
    _center += offset/2.f;
    _radius += std::sqrt(sqDistance)/2.f;
	_squaredRadius = _radius*_radius;
}

float Sphere::volume() const
{
	// V = (4 * pi * R^3)/3
	return (4.f * float(M_PI) * _squaredRadius * _radius) / 3.f;
}

void Sphere::setCenter(const glm::vec3 &center)
{
    _center = center;
}

void Sphere::setRadius(float radius)
{
    _radius = radius;
	_squaredRadius = radius * radius;
}

}


namespace RGL
{

namespace intersect
{

bool check(const bounds::AABB &A, const bounds::AABB &B)
{
	const auto xrange = A.min().x <= B.max().x and A.max().x >= B.min().x;
	const auto yrange = A.min().y <= B.max().y and A.max().y >= B.min().y;
	const auto zrange = A.min().z <= B.max().z and A.max().z >= B.min().z;

	return xrange and yrange and zrange;
}

bool check(const bounds::AABB &box, const bounds::Sphere &sphere)
{
	glm::vec3 closest;
	for(auto axis = 0; axis < 3; ++axis)
		closest[axis] = glm::clamp(sphere.center()[axis], box.min()[axis], box.max()[axis]);

	const auto closest_to_center = closest - sphere.center();

	const auto sq_distance = glm::dot(closest_to_center, closest_to_center);

	return sq_distance <= sphere.radius() * sphere.radius();
}

bool check(const bounds::AABB &box, const glm::vec3 &point)
{
	return point.x > box.min().x and
		point.x < box.max().x and
		point.y > box.min().y and
		point.y < box.max().y and
		point.z > box.min().z and
		point.z < box.max().z;
}

bool check(const bounds::Sphere &sphere, const glm::vec3 &point)
{
	const auto offset = sphere.center() - point;
	const auto sqDistance = glm::dot(offset, offset);
	return sqDistance < sphere.squaredRadius();
}


} // intersect

} // RGL
