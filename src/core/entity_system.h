#pragma once

#include <entt/fwd.hpp>

#include <chrono>

namespace RGL
{

class EntitySystem
{
public:
	EntitySystem() {};

	virtual void register_subscriptions(entt::registry &entities) { (void)entities; };

	virtual void update(entt::registry &entities, std::chrono::nanoseconds frame_time) = 0;
};

} // RGL