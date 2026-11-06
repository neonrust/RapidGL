#pragma once

#include <string>
#include "bounds.h"
#include "plane.h"

#include <glm/vec3.hpp>
#include <TheWisp/signals.hpp>

namespace RGL::trigger
{

class Trigger
{
public:
	Trigger(std::string_view name);

	inline std::string_view name() const { return _name; };

	virtual bool triggeredAt(const glm::vec3 &point) = 0;

	fteng::signal<void(Trigger *)> triggered;

private:
	std::string _name;
};

class AABB : public Trigger
{
public:
	AABB(std::string_view name, const bounds::AABB &aabb);

	bool triggeredAt(const glm::vec3 &point) override;

private:
	bounds::AABB _aabb;
};

class Sphere : public Trigger
{
public:
	Sphere(std::string_view name, const bounds::Sphere &sphere);

	bool triggeredAt(const glm::vec3 &point) override;

private:
	bounds::Sphere _sphere;
};

class HalfSpace : public Trigger
{
public:
	HalfSpace(std::string_view name, const Plane &plane);

	bool triggeredAt(const glm::vec3 &point) override;

private:
	Plane _plane;
};


} // RGL::trigger
