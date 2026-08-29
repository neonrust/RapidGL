#pragma once

#include "bounds.h"
#include "scene_items.h"
#include "room_portal.h"
#include "container_types.h"
#include <string>

namespace RGL
{

// 1. Deduce which room the view (camera) is in, through its AABB.
// 2. Render everything in the current room (associated to it)
// 3. Collect visible portals from the current room (inside frustum and facing camera)
// 4. For each visible portal's room: clip frustum with portal, go to 2

// Probably, instead of immediately renderering, add room + frustum to a "to render" list
// When no more visible portals are found: render the list

// Loading scenes: objects not inside a room, gets added (as before) to the "global" scene.
// Objects inside rooms are added to Scene's "rooms" container.
// Rendering draws both those containers (global last/first?).

class Room : public SceneItems
{
public:
	using PortalList = small_vec<RoomPortal, 16>;

public:
	Room(std::string_view name, const bounds::AABB &aabb);

	inline const bounds::AABB aabb() const { return _aabb; }
	inline std::string_view name() const { return _name; }

	void addPortal(const RoomPortal &portal);  // the room owns the portal
	uint32_t numPortals() const { return uint32_t(_portals.size()); }

	inline PortalList::const_iterator portalsBegin() { return _portals.begin(); }
	inline PortalList::const_iterator portalsEnd()  { return _portals.end(); }

private:
	bounds::AABB _aabb;
	std::string _name;

	PortalList _portals;
};

} // RGL