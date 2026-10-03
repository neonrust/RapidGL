#include "animation_system.h"

#include <entt/entity/registry.hpp>

#include "component/animation.h"
#include "component/transform.h"
#include "log.h"
#include <glm/gtx/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include "formatters_entt.h"
#include "scene.h"

#include <chrono>
using namespace std::literals;


namespace RGL
{

static constexpr auto ident_quat = glm::quat_identity<float, glm::defaultp>();

// static Matrix compute_transform(component::Animation &anim, seconds_f progress_time);


AnimationSystem::AnimationSystem(entt::registry &entities) :
	EntitySystem(entities)
{
}

// ----------------------------------------------------------------------------

entt::entity AnimationSystem::add(std::string_view name,  entt::entity entity_id, const AnimationSetup &A)
{
	assert(not _nameToId.contains(name));
	assert(entity_id != NO_ENTITY_ID);

	component::Animation anim;

	anim.subject_id = entity_id;
	anim.end_state = A.end_state;
	// anim.end_time = TODO
	anim.total_loops = A.total_loops;

	auto &c = anim.curves;
	c.position    = A.position;
	c.orientation = A.orientation;
	c.scale       = A.scale;

	// a mask so we can quickly/easily tell which curves needs to be sampled
	c.curve_mask = 0u
		| ((c.position[0]    and not c.position[0]->empty()?    1u: 0u) << 0)
		| ((c.position[1]    and not c.position[1]->empty()?    1u: 0u) << 1)
		| ((c.position[2]    and not c.position[2]->empty()?    1u: 0u) << 2)
		| ((c.orientation[0] and not c.orientation[0]->empty()? 1u: 0u) << 3)
		| ((c.orientation[1] and not c.orientation[1]->empty()? 1u: 0u) << 4)
		| ((c.orientation[2] and not c.orientation[2]->empty()? 1u: 0u) << 5)
		| ((c.scale[0]       and not c.scale[0]->empty()?       1u: 0u) << 6)
		| ((c.scale[1]       and not c.scale[1]->empty()?       1u: 0u) << 7)
		| ((c.scale[2]       and not c.scale[2]->empty()?       1u: 0u) << 8);
	assert(c.curve_mask > 0);

	auto anim_id = _entities.create();
	_nameToId[std::string(name)] = anim_id;

	_entities.emplace<component::Animation>(anim_id, anim);

	return entity_id;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::play(std::string_view name, seconds_f elapsed)
{
	return play(name, elapsed, glm::vec3{0}, ident_quat);
}

// ----------------------------------------------------------------------------

bool AnimationSystem::play(std::string_view name, seconds_f elapsed, const glm::vec3 &offset)
{
	return play(name, elapsed, offset, ident_quat);
}

// ----------------------------------------------------------------------------

bool AnimationSystem::play(std::string_view name, seconds_f elapsed, const glm::vec3 &offset, const glm::quat &orientation)
{
	auto found = _nameToId.find(name);
	if(found == _nameToId.end())
	{
		Log::warning("AnimSys: play: unknown animation: {}", name);
		assert(false);
		return false;
	}
	const auto anim_id = found->second;

	const auto &anim = _entities.get<component::Animation>(anim_id);

	// const auto active_key = active_key_t{
	// 	.anim_entity_id = anim_id,
	// 	.subject_entity_id = anim.subject_id
	// };

	if(_active.contains(name))
	{
		Log::warning("AnimSys: play: already playing: {} (mesh: {})", name, anim.subject_id);
		assert(false);
		return false;
	}

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

	const auto &transform = _entities.get<component::Transform>(anim.subject_id);

	sampler_ctrl ctrl_ {
		.subject = anim.subject_id,
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
			anim::curve_sampler<>(std::const_pointer_cast<anim::curve_cref<>::element_type>(anim.curves.position[0])),
			anim::curve_sampler<>(std::const_pointer_cast<anim::curve_cref<>::element_type>(anim.curves.position[1])),
			anim::curve_sampler<>(std::const_pointer_cast<anim::curve_cref<>::element_type>(anim.curves.position[2])),
			anim::curve_sampler<>(std::const_pointer_cast<anim::curve_cref<>::element_type>(anim.curves.orientation[0])),
			anim::curve_sampler<>(std::const_pointer_cast<anim::curve_cref<>::element_type>(anim.curves.orientation[1])),
			anim::curve_sampler<>(std::const_pointer_cast<anim::curve_cref<>::element_type>(anim.curves.orientation[2])),
			anim::curve_sampler<>(std::const_pointer_cast<anim::curve_cref<>::element_type>(anim.curves.scale[0])),
			anim::curve_sampler<>(std::const_pointer_cast<anim::curve_cref<>::element_type>(anim.curves.scale[1])),
			anim::curve_sampler<>(std::const_pointer_cast<anim::curve_cref<>::element_type>(anim.curves.scale[2])),
		},
	};
	// if(anim.curves.mask & 0x01)
	// 	ctrl_.samplers = anim::curve_sampler<>(anim.curves.position[0]);
	auto [iter, inserted] = _active.emplace(std::string(name), ctrl_);

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

std::vector<std::string_view> AnimationSystem::names() const
{
	std::vector<std::string_view> name_list;
	name_list.reserve(_nameToId.size());
	for(const auto &[name, id]: _nameToId)
		name_list.push_back(name);
	return name_list;
}

// ----------------------------------------------------------------------------

void AnimationSystem::stop(std::string_view name, bool reset_transform)
{
	auto afound = _nameToId.find(name);
	if(afound == _nameToId.end())
	{
		Log::warning("AnimSys: play: unknown animation: {}", name);
		assert(false);
		return;
	}

	// const auto active_key = active_key_t{
	// 	.anim_entity_id = anim_id,
	// 	.subject_entity_id = subject_id,
	// };

	auto found = _active.find(name);

	if(found == _active.end())
		Log::error("AnimSys: stop: animation: {} isn't playing", name);

	if(reset_transform)
	{
		// if needed to reset partially, 'initial' needs to be pos/ori/scale separately
		//   or simply a component::Transform

		const auto anim_id = afound->second;
		const auto &anim = _entities.get<component::Animation>(anim_id);

		auto transform = _entities.get<component::Transform>(anim.subject_id);

		switch(anim.end_state)
		{
		case anim::EndState::Reset:
		{
			const auto &initial = found->second.initial_transform;
			transform = decltype(transform)(initial);
		}
		break;
		case anim::EndState::Clamp:
		{
			// set transform to last curve point

			auto pos = transform.position();
			if(anim.curves.position[0])
				pos.x = anim.curves.position[0]->curve().back().point.value;
			if(anim.curves.position[1])
				pos.y = anim.curves.position[1]->curve().back().point.value;
			if(anim.curves.position[2])
				pos.z = anim.curves.position[2]->curve().back().point.value;
			if(anim.curves.position[0] or anim.curves.position[1] or anim.curves.position[2])
				transform.set_position(pos);

			auto ori = transform.orientation_xyz();
			if(anim.curves.orientation[0])
				ori.x = anim.curves.orientation[0]->curve().back().point.value;
			if(anim.curves.orientation[1])
				ori.y = anim.curves.orientation[1]->curve().back().point.value;
			if(anim.curves.orientation[2])
				ori.z = anim.curves.orientation[2]->curve().back().point.value;
			if(anim.curves.orientation[0] or anim.curves.orientation[1] or anim.curves.orientation[2])
				transform.set_orientation_xyz(ori);

			auto scale = transform.scale();
			if(anim.curves.scale[0])
				scale.x = anim.curves.scale[0]->curve().back().point.value;
			if(anim.curves.scale[1])
				scale.y = anim.curves.scale[1]->curve().back().point.value;
			if(anim.curves.scale[2])
				scale.z = anim.curves.scale[2]->curve().back().point.value;
			if(anim.curves.scale[0] or anim.curves.scale[1] or anim.curves.scale[2])
				transform.set_scale(scale);
		}
		break;
		}
		_entities.replace<component::Transform>(anim.subject_id, transform);
	}

	_active.erase(found);
}

// ----------------------------------------------------------------------------

bool AnimationSystem::is_playing(std::string_view name) const
{
#if defined(_DEBUG)
	auto found = _nameToId.find(name);
	if(found == _nameToId.end())
	{
		Log::warning("AnimSys: is_playing: unknown animation: {}", name);
		return false;
	}
#endif
	// const auto entity_id = found->second;

	// const auto active_key = active_key_t{
	// 	.anim_entity_id = entity_id,
	// 	.subject_entity_id = subject_id,
	// };

	return _active.contains(name);
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

size_t AnimationSystem::AnimationSetup::num_curves() const
{
	return (position[0]?1:0) + (position[1]?1:0) + (position[2]?1:0) +
		(orientation[0]?1:0) + (orientation[1]?1:0) + (orientation[2]?1:0) +
		(scale[0]?1:0) + (scale[1]?1:0) + (scale[2]?1:0);
}

AnimationSystem::AnimationSetup::operator bool() const
{
	return num_curves() > 0 and end_time > 0s;
}

} // RGL