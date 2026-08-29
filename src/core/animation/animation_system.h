#pragma once

#include <string_view>

#include "animation_curve.h"
#include "entity_system.h"
#include "hash_combine.h"
#include "component/animation.h"

#include <entt/fwd.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

struct active_key_t
{
	entt::entity anim_entity_id;
	entt::entity subject_entity_id;

	bool operator == (const active_key_t &that) const = default;
};

namespace std
{
template<>
struct hash<active_key_t>
{
	[[nodiscard]] inline size_t operator()(const active_key_t &k) const
	{
		size_t h { 0 };
		h = hash_combine(h, uint32_t(k.anim_entity_id));
		h = hash_combine(h, uint32_t(k.subject_entity_id));
		return h;
	}
};
}

namespace RGL
{

class AnimationSystem : public EntitySystem
{
public:
	struct AnimationSetup
	{
		std::array<anim::curve_ref<>, 3> position;
		std::array<anim::curve_ref<>, 3> orientation;
		std::array<anim::curve_ref<>, 3> scale;

		anim::EndState end_state { anim::EndState::Clamp };
		seconds_f end_time;

		static constexpr size_t InfiniteLoops = 0;
		uint32_t total_loops { 1 };

		void clear();
	};
public:
	AnimationSystem(entt::registry &entities);

	entt::entity add(std::string_view name, const AnimationSetup &A);

	bool play(std::string_view name, entt::entity subject_Id, seconds_f elapsed);
	bool play(std::string_view name, entt::entity subject_Id, seconds_f elapsed, const glm::vec3 &offset);
	bool play(std::string_view name, entt::entity subject_id, seconds_f elapsed, const glm::vec3 &offset, const glm::quat &orientation);
	void stop(std::string_view name, entt::entity subject_id, bool reset_transform=false);

	bool is_playing(std::string_view name, entt::entity subject_id) const;

	void update(seconds_f elapsed) override;

private:
	string_map<entt::entity> _nameToId;

	struct sampler_ctrl
	{
		entt::entity subject;

		seconds_f started;
		seconds_f last_update;
		seconds_f end_time;
		size_t loops_done { 0 };

		void reset_hint();
		void tesselate(float density=5.f);
		bool has_tesselation() const;
		seconds_f duration() const;
		seconds_f last_end_time() const;

		struct
		{
			glm::vec3 position;
			glm::vec3 orientation;
			glm::vec3 scale;
		} initial;

		glm::vec3 position_offset;
		glm::quat orientation_offset;

		std::array<anim::curve_sampler<>, 9> samplers;
	};

	dense_map<active_key_t, sampler_ctrl> _active;

	float _default_tesselation { 5.f };
};

} // RGL