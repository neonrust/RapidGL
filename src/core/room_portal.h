#pragma once

#include <memory>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace RGL
{

class Room;

class PortalGeometry
{
public:
	PortalGeometry(const glm::vec3 &center, const glm::vec3 &direction, const glm::vec2 &halfExtent={0.5f, 0.5f});

	inline const glm::vec3 &center() const { return _center; }
	inline const glm::vec3 &direction() const { return _direction; }
	inline float radius() const { return _radius; }
	inline glm::vec2 halfExtent() const { return _halfExtent; }

private:
	glm::vec3 _center { 0.f, 0.f, 0.f };
	glm::vec3 _direction { 0.f, 0.f, 1.f };
	float _radius;
	glm::vec2 _halfExtent;
};

class RoomPortal
{
public:
	RoomPortal(const PortalGeometry &geom);

	void setGeometry(const PortalGeometry &geom);
	inline const PortalGeometry &geometry() const { return _geometry; }

	void setRoomName(std::string_view name);
	inline std::string_view roomName() { return _roomName; }
	void setRoom(std::weak_ptr<Room> room);
	inline std::weak_ptr<Room> room() { return _room; }

	inline operator bool () const { return bool(_room.lock()); }

private:
	PortalGeometry _geometry;
	std::string _roomName;
	std::weak_ptr<Room> _room;
};


class Frustum;

namespace intersect
{

bool check(const Frustum &f, const PortalGeometry &geom);

} // intersect

namespace math
{

// construct a new frustum that sees through the portal
Frustum narrow(const Frustum &f, const PortalGeometry &geom);

} // math

} // RGL