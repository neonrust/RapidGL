#pragma once

#include "entity_system.h"

namespace RGL
{

class MovementSystem : public EntitySystem
{
public:
	MovementSystem() {}
	
	void update(entt::registry &entities, std::chrono::nanoseconds frame_time) override;
};

} // RGL