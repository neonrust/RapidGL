#pragma once

#include "container_types.h"
#include "dynamic_tree.h"
#include <entt/fwd.hpp>
#include <glm/fwd.hpp>


namespace RGL
{

// TODO: instead model as a "trigger system", sending "enter/exit" messages?

class TriggerState
{
public:
	TriggerState(entt::registry &entities, const DynamicTree<entt::entity> &triggerTree);

	// TODO: or a Transform instead of `point` and `direction`
	std::pair<uint32_t, uint32_t> check(const glm::vec3 &point, const glm::vec3 &direction, std::function<bool(entt::entity)> on_enter, std::function<bool(entt::entity)> on_exit);

private:
	entt::registry &_entities;
	const DynamicTree<entt::entity> &_triggerTree;
	dense_set<entt::entity> _triggered;
};

} // RGL
