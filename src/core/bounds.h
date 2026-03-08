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

	explicit AABB(const Sphere &sphere);

	void expand(const glm::vec3 &point);
	void expand(const AABB &aabb);
	void expand(const Sphere &sphere);

	bool empty() const;
	void clear();
	[[nodiscard]] glm::vec3 center() const;
	[[nodiscard]] float volume() const;

	[[nodiscard]] float width() const;     // X-axis
	[[nodiscard]] float height() const;    // Y-axis
	[[nodiscard]] float depth() const;     // Z-axis

	[[nodiscard]] inline const glm::vec3 &min() const { return _min; };
	[[nodiscard]] inline const glm::vec3 &max() const { return _max; };

	[[nodiscard]] inline glm::vec3 &min() { return _min; };
	[[nodiscard]] inline glm::vec3 &max() { return _max; };

	[[nodiscard]] std::array<glm::vec3, 8> corners() const;

	[[nodiscard]] AABB transform(const glm::mat4 &tfm) const;

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

	explicit Sphere(const AABB &aabb);

	void expand(const glm::vec3 &vertex);
	[[nodiscard]] float volume() const;
	[[nodiscard]] inline bool empty() const { return _radius < 0; }
    void clear();
	void setCenter(const glm::vec3 &center);
	void setRadius(float radius);

	[[nodiscard]] inline glm::vec3 center() const { return _center; }
	[[nodiscard]] inline float radius() const { return empty()? 0: _radius; };
	[[nodiscard]] inline float squaredRadius() const { return _squaredRadius; };

	[[nodiscard]] inline operator bool () const { return not empty(); }

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

bool check(const bounds::AABB   &A,       const bounds::AABB   &B);
bool check(const bounds::AABB   &box,     const bounds::Sphere &sphere);
bool check(const bounds::AABB   &box,     const    glm::vec3   &point);
bool check(const bounds::Sphere &sphereA, const bounds::Sphere &sphereB);
bool check(const bounds::Sphere &sphere,  const    glm::vec3   &point);

} // intersect

} // RGL
