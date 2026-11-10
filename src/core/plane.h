#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

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
	void set(const glm::vec4 &normal_and_offset);

	[[nodiscard]] inline glm::vec3 normal() const { return _normal; }
	[[nodiscard]] inline float offset() const { return _offset; }

	[[nodiscard]] inline operator glm::vec4 () const { return { _normal, _offset }; }

	[[nodiscard]] inline operator bool () const { return _normal != glm::vec3(0); }

private:
	glm::vec3 _normal { 0.f };
	float _offset { 0.f };
};


namespace math
{

// signed distance  (plane is facing if positive, otherwise negative)
float distance(const Plane &plane, const glm::vec3 &point);

bool parallel(const Plane &plane1, const Plane &plane2);

inline bool facing(const Plane &plane, const glm::vec3 &point)
{
	return distance(plane, point) >= 0.f;
}

// inline bool intersect(const Plane &plane, const glm::vec3 &point)
// {
// 	return distance(plane, point) == 0.f;
// }

bool intersect(const Plane &A, const Plane &B, const Plane &C, glm::vec3 &point);

} // math

} // RGL
