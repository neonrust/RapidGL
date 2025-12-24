#pragma once

#include <array>
#include <glm/fwd.hpp>
#include <glm/vec3.hpp>

namespace bounds
{

class Sphere;

class AABB
{
public:
	AABB();
	AABB(const glm::vec3 &min, const glm::vec3 &max);

	void expand(const glm::vec3 &point);
	void expand(const AABB &aabb);
	void expand(const Sphere &sphere);

	bool empty() const;
	void clear();
	glm::vec3 center() const;
	float volume() const;

    float width() const;     // X-axis
    float height() const;    // Y-axis
    float depth() const;     // Z-axis

	inline const glm::vec3 &min() const { return _min; };
	inline const glm::vec3 &max() const { return _max; };

	inline glm::vec3 &min() { return _min; };
	inline glm::vec3 &max() { return _max; };

	std::array<glm::vec3, 8> corners() const;

	AABB transform(const glm::mat4 &tfm) const;

protected:
	glm::vec3 _min;
	glm::vec3 _max;
};

inline bool AABB::empty() const
{
	return width() < 0;
}

inline void AABB::clear()
{
	_min = glm::vec3(0);
	_max = glm::vec3(-1, 0, 0);
}


// =======================================================================

class Sphere
{
public:
    Sphere();
	Sphere(const glm::vec3 &center, float radius);

	void expand(const glm::vec3 &vertex);
    float volume() const;
	inline bool empty() const { return _radius < 0; }
    void clear();
	void setCenter(const glm::vec3 &center);
	void setRadius(float radius);

	inline glm::vec3 center() const { return _center; }
	inline float radius() const { return empty()? 0: _radius; };
	inline float squaredRadius() const { return _squaredRadius; };

	inline operator bool () const { return not empty(); }

protected:
	glm::vec3 _center;
    float _radius;
	float _squaredRadius;
};

} // bounds

namespace RGL
{

namespace intersect
{

bool check(const bounds::AABB   &A,      const bounds::AABB   &B);
bool check(const bounds::AABB   &box,    const bounds::Sphere &sphere);
bool check(const bounds::AABB   &box,    const    glm::vec3   &point);
bool check(const bounds::Sphere &sphere, const    glm::vec3   &point);

} // intersect

} // RGL
