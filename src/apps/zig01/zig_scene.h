#pragma once

#include <scene.h>
#include <bounds.h>

#include <entt/fwd.hpp>

namespace RGL
{
class LightManager;
};

class ZigScene : public RGL::Scene
{
public:
	ZigScene(entt::registry &entities, RGL::LightManager &lights);

	entt::entity addTrigger(std::string_view name, const RGL::bounds::AABB     &aabb);
	entt::entity addTrigger(std::string_view name, const RGL::bounds::Sphere   &sphere);
	entt::entity addTrigger(std::string_view name, const RGL::bounds::Cylinder &cylinder);
	// EntityID addTrigger(std::string_view name, const RGL::Plane            &plane);
	entt::entity trigger(std::string_view name) const;

	dense_set<entt::entity> queryTriggers(const RGL::bounds::AABB &aabb);

private:
	struct TriggerProxy
	{
		entt::entity trigger_ent;
		RGL::component::TriggerGeneral general;
	};
	RGL::DynamicTree<TriggerProxy> _trigger_bvh;
	// std::vector<std::pair<Plane, TriggerProxy>> _trigger_half_space;

	string_map<entt::entity> _nameToTrigger;
};