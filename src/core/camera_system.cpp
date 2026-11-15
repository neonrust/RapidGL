#include "camera_system.h"

#include "hash_combine.h"
#include "hash_vec2.h" // IWYU pragma: keep
#include "hash_mat4.h" // IWYU pragma: keep

#include "component/camera_general.h"
#include "component/camera_perspective.h"
#include "component/transform.h"

#include <entt/entity/registry.hpp>

namespace RGL
{

CameraSystem::CameraSystem() :
	EntitySystem()
{
}

void CameraSystem::register_subscriptions(entt::registry &entities)
{
	entities.on_destroy< component::CameraGeneral>().connect<&CameraSystem::_camera_removed>(this);
}

static Frustum makeFrustum(const component::CameraGeneral &cam_general, const component::CameraPerspective &cam_persp, const component::Transform &transform);

void CameraSystem::update(entt::registry &entities, std::chrono::nanoseconds)
{
	// TODO: add support for ortho cameras

	auto cameras = entities.view<component::CameraGeneral, component::CameraPerspective, component::Transform>();

	for(const auto &[cam_ent, cam_general, cam_persp, transform]: cameras.each())
	{
		size_t prev_hash { 0 };
		auto found = _hashes.find(cam_ent);
		if(found != _hashes.end())
			prev_hash = found->second;

		size_t curr_hash = hash(cam_general, cam_persp, transform);

		if(curr_hash != prev_hash)
		{
			auto frustum = makeFrustum(cam_general, cam_persp, transform);
			entities.replace<component::CameraFrustum>(cam_ent, frustum);

			found->second = curr_hash;
		}

		// updateExposure(cam_ent, cam_general);
	}
}

static Frustum makeFrustum(const component::CameraGeneral &cam_general, const component::CameraPerspective &cam_persp, const component::Transform &transform)
{
	// calculate view frustum
	component::CameraFrustum frustum;

	float fov_rad = glm::radians(cam_persp.vertical_fov);
	const auto proj = glm::perspective(fov_rad, cam_general.aspect(), cam_persp.near, cam_persp.far);
	const auto R = glm::mat4_cast(transform.orientation());
	const auto T = glm::translate(glm::mat4(1), -transform.position());
	const auto view = R * T;

	frustum.setFromView(proj, view, transform.position());

	return frustum;
}

size_t CameraSystem::hash(const component::CameraGeneral &g, const component::CameraPerspective &p, const component::Transform &t) const
{
	size_t h { 0 };

	h = hash_combine(h, g.brightness);
	h = hash_combine(h, g.view_size);

	h = hash_combine(h, p.vertical_fov);
	h = hash_combine(h, p.far);
	h = hash_combine(h, p.near);
	h = hash_combine(h, p.focal_length);

	h = hash_combine(h, t.transform());

	return h;
}

void CameraSystem::_camera_removed(entt::registry &, entt::entity cam_ent)
{
	_hashes.erase(cam_ent);
}


} // RGL
