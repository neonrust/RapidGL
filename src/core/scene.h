#pragma once

#include <entt/fwd.hpp>
#include <entt/signal/sigh.hpp>

#include "asset/static_model.h"
#include "container_types.h"
#include "light_manager.h"
#include "scene_items.h"
#include "room.h"


namespace RGL
{

// NOTE: this contains ONLY things that affect visuals

class LightManager;

static constexpr entt::entity NO_ENTITY_ID { std::numeric_limits<entt::entity>::max() };

// Game-related things, spawn, triggers etc, are not here

class Scene : public SceneItems
{
private:
	using RoomMap = string_map<std::unique_ptr<Room>>;

public:
	Scene(entt::registry &entities, LightManager &lights, size_t reserve=0);

	EntityID add(std::shared_ptr<const StaticModel> model, const MaterialCSet &materials, const component::Transform &transforn, bool is_dynamic=false);

	void addRoom(std::string_view roomName, const bounds::AABB &aabb);
	bool hasRoom(std::string_view roomName) const;
	bool removeRoom(std::string_view roomName);

	EntityID add(std::string_view roomName, std::shared_ptr<const StaticModel> model, const MaterialCSet &materials, const component::Transform &transform, bool is_dynamic=false);

	bool addAnimation(EntityID entity_id, std::string_view anim_name);

	void moved(EntityID entity_id, const component::Transform &transform);

	bool remove(EntityID entity_id);
	void rebalance(const glm::vec3 &origin);
	void clear();

	inline       LightManager &lights()       { return _lights; }
	inline const LightManager &lights() const { return _lights; }

private:
	void _create_components(EntityID model_ent, std::shared_ptr<const StaticModel> model, const MaterialCSet &materials, const component::Transform &transform, bool is_dynamic);

private:
	RoomMap _rooms;
	// TODO: need map EntityID -> room ?   e.g. update & remove
	dense_map<entt::entity, std::string> _entityToRoom; // TODO: this is not optimal

	entt::registry &_entities;

	LightManager &_lights;
};

} // RGL
