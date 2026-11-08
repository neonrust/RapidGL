#pragma once

#include "container_types.h"
#include "dynamic_tree.h"
#include <entt/fwd.hpp>
#include <glm/fwd.hpp>


namespace RGL
{

class TriggerState
{
public:
	TriggerState(DynamicTree<entt::entity> &triggerTree);

	std::pair<uint32_t, uint32_t> check(const glm::vec3 &point, std::function<bool(entt::entity)> on_enter, std::function<bool(entt::entity)> on_exit);

private:
	DynamicTree<entt::entity> &_triggerTree;
	dense_set<entt::entity> _triggered;
};

} // RGL
