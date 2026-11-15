#pragma once

#include "container_types.h"

#include "entity_system.h"

namespace RGL
{

namespace component
{
struct CameraGeneral;
struct CameraPerspective;
struct Transform;
} // component

class CameraSystem : public EntitySystem
{
public:
	CameraSystem();

	void register_subscriptions(entt::registry &entities) override;

	void update(entt::registry &entities, std::chrono::nanoseconds frame_time) override;

private:
	// void updateExposure(entt::entity cam_ent, component::CameraGeneral cam_general);

	size_t hash(const component::CameraGeneral &g, const component::CameraPerspective &p, const component::Transform &t) const;

	void _camera_removed(entt::registry &, entt::entity cam_ent);

private:
	dense_map<entt::entity, size_t> _hashes;
};

} // RGL