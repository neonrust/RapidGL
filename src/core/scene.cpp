#include "scene.h"

#include "log.h"
#include "room.h"
#include "component/transform.h"
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
	// _items.reserve(std::max(256ul, reserve));
	// TODO: init the whatever-tree

	_entityToRoom.reserve(256);
	_idToProxy.reserve(256);
}

EntityID Scene::add(std::shared_ptr<const StaticModel> model, const MaterialCSet &materials, const component::Transform &transform, bool is_dynamic)
{
	auto model_ent = _entities.create();

	_create_components(model_ent, model, materials, transform, is_dynamic);

	// TODO: call manually (noy through signals), since adding to a room should add to a different spatial index
	// transform the local bounds into world-space

	spatial_insert(model_ent, model->sphere(), transform, is_dynamic);

	return model_ent;
}

void RGL::Scene::moved(EntityID entity_id, const component::Transform &transform)
{
	const auto &model = _entities.get<component::Model>(entity_id);

	auto found = _entityToRoom.find(entity_id);
	if(found != _entityToRoom.end())
	{
		auto &room = *_rooms[found->second];
		room.spatial_update(entity_id, model->sphere(), transform);
	}
	else
		spatial_update(entity_id, model->sphere(), transform);
}

void Scene::addRoom(std::string_view roomName, const bounds::AABB &aabb)
{
	_rooms.emplace(roomName, std::make_unique<Room>(roomName, aabb));
}

bool Scene::hasRoom(std::string_view roomName) const
{
	return _rooms.contains(roomName);
}

bool Scene::removeRoom(std::string_view roomName)
{
	auto found = _rooms.find(roomName);
	if(found == _rooms.end())
		return false;

	auto &room = *found->second;
	// TODO: remove all entities in the room
	// TODO: remove all entities from '_entityToRoom'

	_rooms.erase(found);

	return true;
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
	_entityToRoom[model_ent] = roomName;

	// TODO: call manually (noy through signals), since adding to a room should add to a different spatial index
	// transform the local bounds into world-space

	auto &room = *found->second;

	room.spatial_insert(model_ent, model->sphere(), transform, is_dynamic);

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

	auto found = _entityToRoom.find(entity_id);
	if(found != _entityToRoom.end())
	{
		auto &room = *_rooms[found->second];
		room.spatial_remove(entity_id);
		_entityToRoom.erase(found);
	}
	else
		spatial_remove(entity_id);

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
	_idToProxy.clear();
	_bvh.clear();
	_rooms.clear();
}

void Scene::_create_components(EntityID model_ent, std::shared_ptr<const StaticModel> model, const MaterialCSet &materials, const component::Transform &transform, bool is_dynamic)
{
	_entities.emplace<component::Transform>  (model_ent, transform);
	_entities.emplace<bool>                  (model_ent, is_dynamic);  // bad idea?
	_entities.emplace<component::MaterialSet>(model_ent, materials);
	_entities.emplace<component::Model>      (model_ent, model);
}

} // RGL
