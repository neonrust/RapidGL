#pragma once

#include <scene.h>
#include <bounds.h>

#include <entt/fwd.hpp>

#include <variant>


namespace RGL
{
class LightManager;
};


struct TriggerParams
{
	uint8_t   trigger_limit { 0 }; // 0 = no limit
	uint8_t   ignore_count  { 0 };
	glm::vec3 facing_direction { 0 };
	float     direction_tolerance { 45.f };

	uint8_t   triggered_count { 0 };

	std::variant<std::monostate, RGL::bounds::AABB, RGL::bounds::Sphere, RGL::bounds::Cylinder, RGL::Plane> shape;
};


class ZigScene : public RGL::Scene
{
public:
	ZigScene(entt::registry &entities, RGL::LightManager &lights);

	entt::entity addTrigger(std::string_view name, const TriggerParams &params);
	entt::entity trigger(std::string_view name) const;

	dense_set<entt::entity> queryTriggers(const RGL::bounds::AABB &aabb);


	// struct TriggerProxy
	// {
	// 	entt::entity trigger_ent;
	// 	RGL::component::TriggerGeneral general;
	// };
	const RGL::DynamicTree<entt::entity> &triggers() const { return _trigger_bvh; }

private:
	// global (rationale; triggers might be connected across rooms)
	RGL::DynamicTree<entt::entity> _trigger_bvh;
	// std::vector<std::pair<Plane, TriggerProxy>> _trigger_half_space;

	string_map<entt::entity> _nameToTrigger;
};