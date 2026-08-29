#include "scene.h"

#include "log.h"
#include "room.h"
#include "component/animation_set.h"
#include "component/model.h"
#include "component/material.h"
#include "light_manager.h"
#include "formatters_entt.h" // IWYU pragma: keep

#include <entt/entity/registry.hpp>


using namespace std::chrono;

namespace RGL
{

Scene::Scene(entt::registry &entities, LightManager &lights, size_t reserve) :
	SceneItems(),
	_entities(entities),
	_lights(lights)
{
	_items.reserve(std::max(256ul, reserve));
	// TODO: init the whatever-tree
}

EntityID Scene::add(std::shared_ptr<const StaticModel> model, const MaterialCSet &materials, const component::Transform &transform, bool is_dynamic)
{
	auto model_ent = _entities.create();

	_create_components(model_ent, model, materials, transform, is_dynamic);

	// TODO: call manually (noy through signals), since adding to a room should add to a different spatial index
	// transform the local bounds into world-space

	_spatial_insert(_items, model_ent, model->sphere(), transform, is_dynamic);

	_need_state_sort = true;

	return model_ent;
}

void RGL::Scene::moved(EntityID entity_id, const component::Transform &transform)
{
	const auto &model = _entities.get<component::Model>(entity_id);

	_spatial_update(_items, entity_id, model->sphere(), transform);
}

void Scene::addRoom(std::string_view roomName, const bounds::AABB &aabb)
{
	_rooms.emplace(roomName, std::make_unique<Room>(roomName, aabb));
}

bool Scene::hasRoom(std::string_view roomName) const
{
	return _rooms.contains(roomName);
}

EntityID Scene::add(std::string_view roomName, std::shared_ptr<const StaticModel> model, const MaterialCSet &materials, const component::Transform &transform, bool is_dynamic)
{
	auto found = _rooms.find(roomName);
	if(found == _rooms.end())
	{
		assert(false);
		return NO_ENTITY_ID;
	}

	auto model_ent = _entities.create();

	_create_components(model_ent, model, materials, transform, is_dynamic);

	// TODO: call manually (noy through signals), since adding to a room should add to a different spatial index
	// transform the local bounds into world-space

	auto &room = *found->second;

	_spatial_insert(room._items, model_ent, model->sphere(), transform, is_dynamic);

	_need_state_sort = true;

	return model_ent;
}

bool Scene::addAnimation(EntityID entity_id, std::string_view anim_name)
{
	auto &anim_set = _entities.get<component::AnimationSet>(entity_id);
	if(anim_set.contains(anim_name))
	{
		Log::warning("Scene: animation already added to entity {}: {}", entity_id, anim_name);
		assert(false);
		return false;
	}

	anim_set.insert(std::string(anim_name));
	_entities.replace<component::AnimationSet>(entity_id, anim_set);

	return true;
}

bool Scene::remove(EntityID entity_id)
{
	_entities.destroy(entity_id);
	// TODO: might be in a room
	_spatial_remove(_items, entity_id);

	_need_state_sort = true;

	return true;
}

void Scene::rebalance(const glm::vec3 &origin)
{
	// TODO: rebalance the whatever-tree
	(void)origin;
}

void Scene::clear()
{
	_entities.clear();
	// TODO: reset the whatever-tree
	_items.clear();
	_rooms.clear();
}

void Scene::sortByState()
{
	if(_need_state_sort)
	{
		// TODO: EnTT multi-component sorting: by mesh AND material(hash)
		//   _entities.sort<component::MaterialSet>([](const auto &A, const auto &B) { ... });

		_need_state_sort = false;
	}
}

void Scene::_create_components(EntityID model_ent, std::shared_ptr<const StaticModel> model, const MaterialCSet &materials, const component::Transform &transform, bool is_dynamic)
{
	_entities.emplace<component::Transform>  (model_ent, transform);
	_entities.emplace<bool>                  (model_ent, is_dynamic);  // bad idea?
	_entities.emplace<component::MaterialSet>(model_ent, materials);
	_entities.emplace<component::Model>      (model_ent, model);
}

void Scene::_spatial_insert(SpatialItems items, EntityID entity_id, const bounds::Sphere &local_bounds, const component::Transform &transform, bool is_dynamic)
{
	assert(not items.contains(entity_id));

	auto world_bounds = local_bounds;
	world_bounds.setCenter(glm::mat4(transform) * glm::vec4(world_bounds.center(), 1));
	world_bounds.setRadius(world_bounds.radius() * transform.max_scale());

	// TODO: update the whatever-tree
	// TODO: component with model meta info
	items[entity_id] = { world_bounds, is_dynamic };
}

void Scene::_spatial_update(SpatialItems items, EntityID entity_id, const bounds::Sphere &local_bounds, const component::Transform &transform)
{
	assert(items.contains(entity_id));

	auto world_bounds = local_bounds;
	world_bounds.setCenter(glm::mat4(transform) * glm::vec4(world_bounds.center(), 1));
	world_bounds.setRadius(world_bounds.radius() * transform.max_scale());

	items[entity_id].bounds = world_bounds;
}

void Scene::_spatial_remove(SpatialItems items, EntityID entity_id)
{
	assert(items.contains(entity_id));

	items.erase(entity_id);
}

} // RGL
