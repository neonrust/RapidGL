#include "zig_scene.h"

#include "formatters_glm.h"

using namespace RGL;

ZigScene::ZigScene(entt::registry &entities, LightManager &lights) :
	Scene(entities, lights),
	_trigger_bvh(64u)
{
	_nameToTrigger.reserve(64u);
}

entt::entity ZigScene::addTrigger(std::string_view name, const bounds::AABB &aabb)
{
	assert(not _nameToTrigger.contains(name));

	const auto trigger_ent = entities().create();
	_nameToTrigger[name] = trigger_ent;

	component::TriggerGeneral general(component::TriggerShape::AABB);
	general.name = name;

	auto centered_aabb = aabb;
	// remove center offset from the AABB (use Transform for that)
	centered_aabb.min() -= aabb.center();
	centered_aabb.max() -= aabb.center();
	assert(glm::length(centered_aabb.center()) < 1e-5); // the new center should be ~zero
	component::Transform transform;
	transform.set_position(aabb.center());

	entities().emplace<component::TriggerShapeAABB>(trigger_ent, centered_aabb);
	entities().emplace<component::Transform>(trigger_ent, transform);
	entities().emplace<component::TriggerGeneral>(trigger_ent, general);

	// add world-space AABB to BVH
	_trigger_bvh.addProxy(aabb, trigger_ent);//bvhProxy);

	return trigger_ent;
}

entt::entity ZigScene::addTrigger(std::string_view name, const bounds::Sphere &sphere)
{
	assert(not _nameToTrigger.contains(name));

	const auto trigger_ent = entities().create();
	_nameToTrigger[name] = trigger_ent;

	component::TriggerGeneral general(component::TriggerShape::Sphere);
	general.name = name;

	component::Transform transform;
	transform.set_position(sphere.center());

	entities().emplace<component::TriggerShapeSphere>(trigger_ent, bounds::Sphere{ glm::vec3(0), sphere.radius() });
	entities().emplace<component::Transform>(trigger_ent, transform);
	entities().emplace<component::TriggerGeneral>(trigger_ent, general);

	// add world-space AABB to BVH
	_trigger_bvh.addProxy(math::aabb_cast(sphere), trigger_ent);//bvhProxy);

	return trigger_ent;
}

entt::entity ZigScene::addTrigger(std::string_view name, const bounds::Cylinder &cylinder)
{
	assert(not _nameToTrigger.contains(name));

	const auto trigger_ent = entities().create();
	_nameToTrigger[name] = trigger_ent;

	component::TriggerGeneral general(component::TriggerShape::Cylinder);
	general.name = name;

	component::Transform transform;
	transform.set_position(cylinder.center());
	transform.set_direction(glm::normalize(cylinder.halfExtent())); // probably not correct
	auto centered_cylinder = cylinder;
	centered_cylinder.setCenter(glm::vec3(0));

	entities().emplace<component::TriggerShapeCylinder>(trigger_ent, centered_cylinder);
	entities().emplace<component::Transform>(trigger_ent, transform);
	entities().emplace<component::TriggerGeneral>(trigger_ent, general);

	// add world-space AABB to BVH
	_trigger_bvh.addProxy(math::aabb_cast(cylinder), trigger_ent);//bvhProxy);

	return trigger_ent;
}

/*
entt::entity ZigSceneScene::addTrigger(std::string_view name, const Plane &plane)
{
	assert(not _nameToTrigger.contains(name));

 const auto trigger_ent = entities().create();
	_nameToTrigger[name] = trigger_ent;

 component::TriggerGeneral general{ .shape = component::TriggerShape::HalfSpace };

 component::Transform transform;
 transform.set_orientation(plane.normal()); // probably not correct
 transform.set_position({ 0, 0, plane.offset() }); // definitely not correct

 entities().emplace<component::TriggerShapeSphere>(trigger_ent, AXIS_Z, 0);  // probably not correct
 entities().emplace<component::Transform>(trigger_ent, transform);
 entities().emplace<component::TriggerGeneral>(trigger_ent, general);

 auto bvhProxy = TriggerProxy { .trigger_ent = trigger_ent, .general = general };
 _trigger_half_space.push_back({plane, bvhProxy});

 return trigger_ent;
}
*/
entt::entity ZigScene::trigger(std::string_view name) const
{
	auto found = _nameToTrigger.find(name);
	if(found != _nameToTrigger.end())
		return found->second;

	return NO_ENTITY_ID;
}

dense_set<entt::entity> ZigScene::queryTriggers(const bounds::AABB &aabb)
{
	static constexpr uint64_t MASK_ALL = std::numeric_limits<uint64_t>::max();

	dense_set<entt::entity> result;
	result.reserve(2);
	_trigger_bvh.query(aabb, MASK_ALL, false, [&](auto, auto, auto trigger_ent) {
		result.insert(trigger_ent);
		return true;
	});

		   // TODO: half-space triggers

	return result;
}
