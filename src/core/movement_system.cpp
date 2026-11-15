#include "movement_system.h"

#include <entt/entity/registry.hpp>

#include "component/movement.h"
#include "component/transform.h"
#include "game_time.h"

namespace RGL
{

void MovementSystem::update(entt::registry &entities, std::chrono::nanoseconds frame_time)
{
	const auto frame_time_f = float(duration_cast<seconds_f>(frame_time).count());

	auto objects = entities.view<component::Transform, component::Movement>();

	for(const auto &[entity_id, transform, movement]: objects.each())
	{
		bool clear_end_pos = false;

		if(movement.velocity != glm::vec3(0))
		{
			const auto move = movement.velocity * frame_time_f;
			auto position = transform.position() + move;

			if(movement.end_position.has_value())
			{
				auto end = movement.end_position.value();

				if((move.x > 0 and position.x > end.x) or (move.x < 0 and position.x < end.x))
					position.x = end.x;
				if((move.y > 0 and position.y > end.y) or (move.y < 0 and position.y < end.y))
					position.y = end.y;
				if((move.z > 0 and position.z > end.z) or (move.z < 0 and position.z < end.z))
					position.z = end.z;

				if(position == end)  // end position has been reached, clear 'end_position'
					clear_end_pos = true;
			}

			transform.set_position(position);

			entities.replace<component::Transform>(entity_id, transform);
		}
		else if(movement.end_position.has_value())
		{
			transform.set_position(movement.end_position.value());
			// end position has been reached, clear 'end_position'
			clear_end_pos = true;
		}

		if(clear_end_pos)
		{
			movement.end_position = std::nullopt;
			entities.replace<component::Movement>(entity_id, movement);
		}
	}
}

} // RGL