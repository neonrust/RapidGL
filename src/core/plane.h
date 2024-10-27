#pragma once

#include <glm/vec3.hpp>

namespace RGL
{

struct Plane
{
	inline Plane() {}
	inline Plane(const glm::vec3 &normal, float offset) :
		_normal(normal),
		_offset(offset)
	{
	}

	void set(const glm::vec3 &normal, float offset);

	inline glm::vec3 normal() const { return _normal; }
	inline float offset() const { return _offset; }

private:
	glm::vec3 _normal { 0.f, 1.f, 0.f };
	float _offset { 0.f };
};


namespace math
{

float distance(const Plane &plane, const glm::vec3 &point);

bool parallel(const Plane &plane1, const Plane &plane2);

inline bool intersects(const Plane &plane, const glm::vec3 &point)
{
	return distance(plane, point) == 0.f;
}

inline bool facing(const Plane &plane, const glm::vec3 &point)
{
	return distance(plane, point) >= 0.f;
}

} // math

namespace intersect
{

bool check(const Plane &a, const Plane &b, const Plane &c, glm::vec3 *point=nullptr);

} // intersect

} // RGL
