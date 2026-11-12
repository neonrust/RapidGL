#include "trigger_state.h"

#include "dynamic_tree.h"
#include "component/trigger.h"

#include <entt/entity/registry.hpp>
#include <glm/trigonometric.hpp>

namespace RGL
{

TriggerState::TriggerState(entt::registry &entities, const DynamicTree<entt::entity> &triggerTree) :
	_entities(entities),
	_triggerTree(triggerTree)
{
	_triggered.reserve(4u);
}

std::pair<uint32_t, uint32_t> TriggerState::check(const glm::vec3 &point, const glm::vec3 &direction, std::function<bool (entt::entity)> on_enter, std::function<bool (entt::entity)> on_exit)
{
	uint32_t num_entered { 0 };
	uint32_t num_exited { 0 };

	static decltype(_triggered) currently_triggering;
	currently_triggering.reserve(4u);
	currently_triggering.clear();

	_triggerTree.query(point, ~0u, false, [&](auto, auto, entt::entity trigger_ent) {

		auto general = _entities.get<component::TriggerGeneral>(trigger_ent);
		if(not general.enabled)
			return true;
		if(general.facing_direction != glm::vec3(0))
		{
			assert(std::abs(glm::length(direction) - 1.f) < 1e-4);
			float diff_angle = glm::degrees(std::acos(glm::dot(general.facing_direction, direction)));
			if(diff_angle > general.direction_tolerance)
				return true;
		}

		// currently in triggered state, but wasn't before -> ENTERED
		if(not _triggered.contains(trigger_ent))
		{
			// first time entering triggering state, check counters
			if(general.ignore_count > 0)
			{
				--general.ignore_count;
				_entities.replace<component::TriggerGeneral>(trigger_ent, general);
				return true;
			}
			if(general.trigger_limit > 0 and general.triggered_count < general.trigger_limit)
			{
				++general.triggered_count;
				_entities.replace<component::TriggerGeneral>(trigger_ent, general);
				return true;
			}

			_triggered.insert(trigger_ent);
			++num_entered;
			on_enter(trigger_ent);
		}

		currently_triggering.insert(trigger_ent);

		return true;
	});

	// any trigger that was triggering before, but is no longer ->  exit
	for(const auto trigger_ent: _triggered)
	{
		if(not currently_triggering.contains(trigger_ent))
		{
			// currently not in triggered state, but was before -> EXITED
			_triggered.erase(trigger_ent);
			++num_exited;
			on_exit(trigger_ent);
		}
	}

	return { num_entered, num_exited };
}

} // RGL