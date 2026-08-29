#include "animation_system.h"

#include <entt/entity/registry.hpp>

#include "component/animation.h"
#include "component/transform.h"
#include "log.h"
#include <glm/gtx/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include "formatters_entt.h"



namespace RGL
{

static constexpr auto ident_quat = glm::quat_identity<float, glm::defaultp>();

// static Matrix compute_transform(component::Animation &anim, seconds_f progress_time);


AnimationSystem::AnimationSystem(entt::registry &entities) :
	EntitySystem(entities)
{
}

// ----------------------------------------------------------------------------

entt::entity AnimationSystem::add(std::string_view name,  const AnimationSetup &A)
{
	assert(not _nameToId.contains(name));

	auto entity_id = _entities.create();
	_nameToId[std::string(name)] = entity_id;

	component::Animation anim;

	auto &c= anim.curves;
	c.position =    A.position;
	c.orientation = A.orientation;
	c.scale =       A.scale;

	// a mask so we can quickly/easily tell which curves needs to be sampled
	c.mask = 0
		| (c.position[0]->empty()? 0:    1 << 0)
		| (c.position[1]->empty()? 0:    1 << 1)
		| (c.position[2]->empty()? 0:    1 << 2)
		| (c.orientation[0]->empty()? 0: 1 << 3)
		| (c.orientation[1]->empty()? 0: 1 << 4)
		| (c.orientation[2]->empty()? 0: 1 << 5)
		| (c.scale[0]->empty()? 0:       1 << 6)
		| (c.scale[1]->empty()? 0:       1 << 7)
		| (c.scale[2]->empty()? 0:       1 << 8);
	assert(c.mask > 0);

	anim.end_state = A.end_state;
	anim.total_loops = A.total_loops;

	return entity_id;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::play(std::string_view name, entt::entity subject_Id, seconds_f elapsed)
{
	return play(name, subject_Id, elapsed, glm::vec3{0}, ident_quat);
}

// ----------------------------------------------------------------------------

bool AnimationSystem::play(std::string_view name, entt::entity subject_Id, seconds_f elapsed, const glm::vec3 &offset)
{
	return play(name, subject_Id, elapsed, offset, ident_quat);
}

// ----------------------------------------------------------------------------

bool AnimationSystem::play(std::string_view name, entt::entity subject_id, seconds_f elapsed, const glm::vec3 &offset, const glm::quat &orientation)
{
	auto found = _nameToId.find(name);
	if(found == _nameToId.end())
	{
		Log::warning("AnimSys: play: unknown animation: {}", name);
		assert(false);
		return false;
	}
	const auto entity_id = found->second;

	const auto active_key = active_key_t{
		.anim_entity_id = entity_id,
		.subject_entity_id = subject_id,
	};

	if(_active.contains(active_key))
	{
		Log::warning("AnimSys: play: already playing: {} for mesh {}", name, uint32_t(subject_id));
		assert(false);
		return false;
	}

	const auto &anim = _entities.get<component::Animation>(entity_id);

	seconds_f end_time { 0 };
	for(const auto idx: { 0u, 1u, 2u })
	{
		if(not anim.curves.position[idx]->empty())
			end_time = std::max(end_time, anim.curves.position[idx]->end_time());
		if(not anim.curves.orientation[idx]->empty())
			end_time = std::max(end_time, anim.curves.orientation[idx]->end_time());
		if(not anim.curves.scale[idx]->empty())
			end_time = std::max(end_time, anim.curves.scale[idx]->end_time());
	}

	const auto &transform = _entities.get<component::Transform>(subject_id);

	sampler_ctrl ctrl_ {
		.subject = subject_id,
		.started = elapsed,
		.last_update = elapsed,
		.end_time = end_time,
		// .initial = {  // or just the transform component, as-is?  not if pos/ori/scale needs to be reset individually
		// 	.position = transform.position(),
		// 	.orientation = transform.orientation_xyz(),
		// 	.scale = transform.scale(),
		// },
		.initial_transform = transform.transform(),
		.position_offset = offset,
		.orientation_offset = orientation,
		.samplers = {
			anim::curve_sampler<>(anim.curves.position[0]),
			anim::curve_sampler<>(anim.curves.position[1]),
			anim::curve_sampler<>(anim.curves.position[2]),
			anim::curve_sampler<>(anim.curves.orientation[0]),
			anim::curve_sampler<>(anim.curves.orientation[1]),
			anim::curve_sampler<>(anim.curves.orientation[2]),
			anim::curve_sampler<>(anim.curves.scale[0]),
			anim::curve_sampler<>(anim.curves.scale[1]),
			anim::curve_sampler<>(anim.curves.scale[2]),
		},
	};
	// if(anim.curves.mask & 0x01)
	// 	ctrl_.samplers = anim::curve_sampler<>(anim.curves.position[0]);
	auto [iter, inserted] = _active.emplace(active_key, ctrl_);

	auto &ctrl = iter->second;
	if(_default_tesselation > 0)
		ctrl.tesselate(_default_tesselation);

	return true;
}

// ----------------------------------------------------------------------------

void AnimationSystem::update(seconds_f elapsed)
{
	for(const auto &[name, ctrl]: _active)
	{
		// TODO
/*
		bool done = false;

		// update each brush
		for(auto &ent: found->second)
		{
			auto [anim, transform] = _entities.get<component::Animation, component::Transform>(ent);
			assert(anim.is_playing());

			auto progress_time = elapsed - anim.started - anim.end_time*anim.loops_done;
			anim.last_update = elapsed;

			// handle looping
			if(progress_time > anim.end_time)
			{
				while(progress_time > anim.end_time and (anim.loops_done < anim.num_loops or anim.num_loops == component::Animation::InfiniteLoops))
				{
					progress_time -= anim.end_time;
					anim.curves.reset();
					++anim.loops_done;
					std::cout << "animation '" << name << "' END  loops: " << anim.loops_done << " / " << anim.num_loops << "  t: " << progress_time << '\n';
				}

				if(anim.num_loops != component::Animation::InfiniteLoops and anim.loops_done >= anim.num_loops)
					done = true;
			}

			auto m = compute_transform(anim, progress_time);
			transform = m;
		}

		if(done)
			stop(name); // stops all brushes associated to this animation
*/
	}
}

// ----------------------------------------------------------------------------

void AnimationSystem::stop(std::string_view name, entt::entity subject_id, bool reset_transform)
{
	auto afound = _nameToId.find(name);
	if(afound == _nameToId.end())
	{
		Log::warning("AnimSys: play: unknown animation: {}", name);
		assert(false);
		return;
	}
	const auto anim_id = afound->second;

	const auto active_key = active_key_t{
		.anim_entity_id = anim_id,
		.subject_entity_id = subject_id,
	};

	auto found = _active.find(active_key);

	if(found == _active.end())
		Log::error("AnimSys: stop: animation: {} isn't playing on entity {}", name, subject_id);

	if(reset_transform)
	{
		// if needed to reset partially, 'initial' needs to be pos/ori/scale separately
		//   or simply a component::Transform

		auto transform = _entities.get<component::Transform>(subject_id);
		// const auto &initial = found->second.initial;
		const auto &initial = found->second.initial_transform;
		// transform.set_position(initial.position);
		// transform.set_orientation_xyz(initial.orientation);
		// transform.set_scale(initial.scale);
		transform = decltype(transform)(initial);
		_entities.replace<component::Transform>(subject_id, transform);
	}

	_active.erase(found);
}

// ----------------------------------------------------------------------------

bool AnimationSystem::is_playing(std::string_view name, entt::entity subject_id) const
{
	auto found = _nameToId.find(name);
	if(found == _nameToId.end())
	{
		Log::warning("AnimSys: is_playing: unknown animation: {}", name);
		return false;
	}
	const auto entity_id = found->second;

	const auto active_key = active_key_t{
		.anim_entity_id = entity_id,
		.subject_entity_id = subject_id,
	};

	return _active.contains(active_key);
}

// ----------------------------------------------------------------------------
/*
glm::mat4 compute_transform(component::Animation &anim, seconds_f progress_time)
{
	Vector3 position = anim.original.position;
	Vector3 rotation = anim.original.rotation;
	Vector3 scale = anim.original.scale;

	if(not anim.curves.position.empty())
		anim.curves.position.sample_at(progress_time, position);
	if(not anim.curves.rotation.empty())
		anim.curves.rotation.sample_at(progress_time, position);
	if(not anim.curves.scale.empty())
		anim.curves.scale.sample_at(progress_time, scale);

	// std::cout << progress_time << "  --> " << position << '\n';

	// combine position, rotation & scale into a transformation matrix
	const auto mtxTranslate = MatrixTranslate(position.x, position.y, position.z);
	const auto rotationRad = Vector3Scale(rotation, std::numbers::pi_v<float>/180.f);
	const auto mtxRotate = MatrixRotateXYZ(rotationRad);
	const auto mtxScale = MatrixScale(scale.x, scale.y, scale.z);

	// M = R * S * T
	return MatrixMultiply(MatrixMultiply(mtxScale, mtxRotate), mtxTranslate);
}
*/
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

void AnimationSystem::sampler_ctrl::reset_hint()
{
	for(auto &sampler: samplers)
		sampler.reset_hint();
}

void AnimationSystem::sampler_ctrl::tesselate(float density)
{
	for(auto &sampler: samplers)
	{
		if(sampler.curve() and not sampler.curve()->empty())
			sampler.tesselate(density);
	}
}

// ----------------------------------------------------------------------------

bool AnimationSystem::sampler_ctrl::has_tesselation() const
{
	auto total = 0u;
	for(auto &sampler: samplers)
	{
		if(sampler.curve() and not sampler.curve()->empty())
			total += sampler.tesselation_samples();
	}
	return total > 0;
}

// ----------------------------------------------------------------------------

seconds_f AnimationSystem::sampler_ctrl::duration() const
{
	seconds_f d { 0 };
	for(const auto &sampler: samplers)
	{
		if(sampler.curve() and not sampler.curve()->empty())
			d = std::max(d, sampler.curve()->duration());
	}
	return d;
}

seconds_f AnimationSystem::sampler_ctrl::last_end_time() const
{
	seconds_f end { 0 };
	for(const auto &sampler: samplers)
	{
		if(sampler.curve() and not sampler.curve()->empty())
			end = std::max(end, sampler.curve()->end_time());
	}
	return end;
}

void AnimationSystem::AnimationSetup::clear()
{
	for(auto idx: { 0u, 1u, 2u })
	{
		position[idx].reset();
		orientation[idx].reset();
		scale[idx].reset();
	}
	end_state = anim::EndState::Clamp;
	end_time = seconds_f(0);
	total_loops = InfiniteLoops;
}

} // RGL