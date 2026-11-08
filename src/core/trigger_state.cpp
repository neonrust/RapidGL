#include "trigger_state.h"

#include "dynamic_tree.h"

namespace RGL
{

TriggerState::TriggerState(DynamicTree<entt::entity> &triggerTree) :
	_triggerTree(triggerTree)
{
	_triggered.reserve(4u);
}

std::pair<uint32_t, uint32_t> TriggerState::check(const glm::vec3 &point, std::function<bool (entt::entity)> on_enter, std::function<bool (entt::entity)> on_exit)
{
	uint32_t num_entered { 0 };
	uint32_t num_exited { 0 };

	decltype(_triggered) entered;
	entered.reserve(4u);

	_triggerTree.query(point, ~0u, false, [&](auto, auto, entt::entity trigger_ent) {

		if(not _triggered.contains(trigger_ent))
		{
			_triggered.insert(trigger_ent);
			entered.insert(trigger_ent);
			++num_entered;

			on_enter(trigger_ent);
		}
		return true;
	});

	for(const auto trigger_ent: _triggered)
	{
		if(not entered.contains(trigger_ent))
		{
			_triggered.erase(trigger_ent);
			++num_exited;

			on_exit(trigger_ent);
		}
	}

	return { num_entered, num_exited };
}

} // RGL