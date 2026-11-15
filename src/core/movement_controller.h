#pragma once

#include "control_map.h"

#include <entt/entity/fwd.hpp>


namespace RGL
{

class MovementController
{
public:
	inline MovementController(entt::entity subject_id) : _subject_id(subject_id) {}

	entt::entity set_entity(entt::entity new_subject) {
		std::swap(_subject_id, new_subject);
		return new_subject;
	}
	entt::entity subject() const { return _subject_id; }

	virtual void update(entt::registry &entities, std::chrono::nanoseconds deltaTime) = 0;

private:
	ControlMap _controls;
	entt::entity _subject_id;
};

} // RGL