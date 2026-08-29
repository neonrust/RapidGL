#include "room.h"

namespace RGL
{

Room::Room(std::string_view name, const bounds::AABB &aabb) :
	_aabb(aabb),
	_name(name)
{
}

void Room::addPortal(const RoomPortal &portal)
{
	_portals.push_back(portal); // copy
}



} // RGL