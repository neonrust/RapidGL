#pragma once

#include <entt/fwd.hpp>

#include "game_time.h"

namespace RGL
{

class EntitySystem
{
public:
	EntitySystem(entt::registry &entities) : _entities(entities) {};

	virtual void update(seconds_f elapsed) = 0;

protected:
	entt::registry &_entities;
};

} // RGL