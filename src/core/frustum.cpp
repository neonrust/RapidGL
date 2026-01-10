#include "frustum.h"
#include <functional>  // std::ref
#include <glm/mat4x4.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>
#include "bounds.h"

namespace RGL
{

void Frustum::setFromProjection(const glm::mat4 &proj, const glm::vec3 &origin)
{
	setFromView(proj, glm::mat4(1), origin);
}

static glm::vec3 intersection(const Plane &A, const Plane &B, const Plane &C, const glm::vec3 &crossAB, const glm::vec3 &crossAC, const glm::vec3 &crossBC)
{
	const auto denom = -glm::dot(A.normal(), crossBC);

	const auto nom = glm::mat3(crossBC, -crossAC, crossAB) *
		glm::vec3(A.offset(), B.offset(), C.offset());

	return nom / denom;
}

void Frustum::setFromView(const glm::mat4 &proj, const glm::mat4 &view, const glm::vec3 &origin)
{
	_origin = origin;

	// transpose to make it easier to extract the frustum plane vectors
	const auto mvp = glm::transpose(proj * view);

	const glm::vec4 anchor { mvp[3] };

	// this sets UNnormalized values; they're needed in the corners & AABB calculation below.
	//   normals will be normalized at the end
	_left.set(  anchor + mvp[0]);
	_right.set( anchor - mvp[0]);
	_bottom.set(anchor + mvp[1]);
	_top.set(   anchor - mvp[1]);
	_near.set(  anchor + mvp[2]);
	_far.set(   anchor - mvp[2]);

	// build an AABB around the frustum for even faster early-outs

	// compute the 8 corners of the frustum by intersecting the 3 adjacent planes
	const glm::vec3 crosses[] = {
		{}, // glm::cross(_left.normal(),   _right.normal()), // 0
		glm::cross(_left.normal(),   _bottom.normal()),  // 1
		glm::cross(_left.normal(),   _top.normal()),     // 2
		glm::cross(_left.normal(),   _near.normal()),   // 3
		glm::cross(_left.normal(),   _far.normal()),    // 4
		glm::cross(_right.normal(),  _bottom.normal()),  // 5
		glm::cross(_right.normal(),  _top.normal()),     // 6
		glm::cross(_right.normal(),  _near.normal()),   // 7
		glm::cross(_right.normal(),  _far.normal()),    // 8
		{}, // glm::cross(_bottom.normal(), _top.normal()),  // 9
		glm::cross(_bottom.normal(), _near.normal()),   // 10
		glm::cross(_bottom.normal(), _far.normal()),    // 11
		glm::cross(_top.normal(),    _near.normal()),   // 12
		glm::cross(_top.normal(),    _far.normal()),    // 13
		{} // glm::cross(_front.normal(),  _back.normal())   // 14
	};

	_corners = {
		intersection(_left,  _top,    _near, crosses[2], crosses[3], crosses[12]),
		intersection(_left,  _bottom, _near, crosses[1], crosses[3], crosses[10]),
		intersection(_left,  _top,    _far,  crosses[2], crosses[4], crosses[13]),
		intersection(_left,  _bottom, _far,  crosses[1], crosses[4], crosses[11]),
		intersection(_right, _top,    _near, crosses[6], crosses[7], crosses[12]),
		intersection(_right, _bottom, _near, crosses[5], crosses[7], crosses[10]),
		intersection(_right, _top,    _far,  crosses[6], crosses[8], crosses[13]),
		intersection(_right, _bottom, _far,  crosses[5], crosses[8], crosses[11])
	};

	_aabb.clear();
	for(const auto &corner: _corners)
		_aabb.expand(corner);

	// normalize planes after computing corners & AABB
	for(Plane &plane: std::array{std::ref(_left), std::ref(_right), std::ref(_bottom), std::ref(_top), std::ref(_near), std::ref(_far)})
	{
		const auto l = glm::length(plane.normal());
		plane.set(plane.normal() / l, plane.offset() / l);
	}
}

glm::vec3 Frustum::center() const
{
	return (  _corners[0]
			+ _corners[1]
			+ _corners[2]
			+ _corners[3]
			+ _corners[4]
			+ _corners[5]
			+ _corners[6]
			+ _corners[7]) / 8.f;
}

const std::array<glm::vec4, 6> Frustum::planes() const
{
	return { left(), right(), top(), bottom(), near(), far() };
}

namespace intersect
{

frustum_cull_result check(const Frustum &f, const bounds::AABB &box, const glm::mat4 &box_transform)
{
	// https://iquilezles.org/articles/frustumcorrect/

	// TODO: should first check whether f.origin() is inside the AABB

	frustum_cull_result result;

	// check if any of the *transformed* 8 corners of the box is contained by the frustum
	bounds::AABB tfm_aabb;
	for(const auto &corner: box.corners())
		tfm_aabb.expand(box_transform * glm::vec4(corner, 1));

	// if it's not inside the frustum's AABB, it's definitely not visible
	if(not check(f.aabb(), tfm_aabb))
	{
		// std::print("  CULLED by AABB {}/{}\n", glm::to_string(minp).c_str(), glm::to_string(maxp).c_str());
		result.visible = false;
		result.culled_by_aabb = true;
		return result;
	}

	const auto &box_corners = tfm_aabb.corners();

	decltype(frustum_cull_result::culled_by_plane) cull_plane = 0;
	for(const auto &plane: { f.left(), f.right(), f.top(), f.bottom(), f.near(), f.far() })
	{
		for(const auto &corner: box_corners)
		{
			const auto dist = math::distance(plane, corner);

			if(result.distance_to_plane[size_t(cull_plane)] == frustum_cull_result::UNUSED or dist < result.distance_to_plane[size_t(cull_plane)])
				result.distance_to_plane[size_t(cull_plane)] = dist;
		}
		++cull_plane;
	}

	cull_plane = 0;
	for(const auto &plane: { f.left(), f.right(), f.top(), f.bottom(), f.near(), f.far() })
	{
		bool potentially_inside = false;
		for(const auto &corner: box_corners)
		{
			if(math::facing(plane, corner)) // i.e. if inside the frustum volume (the planes face inwards)
			{
				potentially_inside = true;
				break;
			}
		}
		if(not potentially_inside) // i.e. all points outside
		{
			result.visible = false;
			result.culled_by_plane = cull_plane;
			return result;
		}
		++cull_plane;
	}

	// check frustum outside/inside box

	const auto &box_min = tfm_aabb.min();
	const auto &box_max = tfm_aabb.max();

	// check each axis; wether the frustum corners are all outside the AABB box
	std::uint_fast8_t out = 0;
	result.visible = false;

	for(const auto &corner: f.corners())  out += (corner.x > box_max.x)? 1: 0;
	if(out == 8) return result;
	out = 0;
	for(const auto &corner: f.corners())  out += (corner.x < box_min.x)? 1: 0;
	if(out == 8) return result;
	out = 0;
	for(const auto &corner: f.corners())  out += (corner.y > box_max.y)? 1: 0;
	if(out == 8) return result;
	out = 0;
	for(const auto &corner: f.corners())  out += (corner.y < box_min.y)? 1: 0;
	if(out == 8) return result;
	out = 0;
	for(const auto &corner: f.corners())  out += (corner.z > box_max.z)? 1: 0;
	if(out == 8) return result;
	out = 0;
	for(const auto &corner: f.corners())  out += (corner.z < box_min.z)? 1: 0;
	if(out == 8) return result;

	// some of the frustum's points were inside the AABB
	result.visible = true;
	return result;
}

bool check(const Frustum &f, const glm::vec3 &point)
{
	if(not intersect::check(f.aabb(), point)) // early-out using the frustum's AABB
		return false;

	// true if the 'point' is "in front of"" all planes
	return math::facing(f.far(), point) and math::facing(f.near(), point)
		and math::facing(f.left(), point) and math::facing(f.right(), point)
		and math::facing(f.top(), point) and math::facing(f.bottom(), point);
}

bool check(const Frustum &f, const bounds::Sphere &sphere)
{
	const auto distance_sq = glm::distance2(f.origin(), sphere.center());
	if(distance_sq < sphere.squaredRadius())
		return true;

	// early-out: sphere outside frustum's AABB
	if(not intersect::check(f.aabb(), sphere))
		return false;

	// https://wickedengine.net/2018/01/optimizing-tile-based-light-culling/
	// https://gamedev.stackexchange.com/a/86010

#define PLANE_TEST(_name_) \
	{ const auto d = math::distance(f._name_(), sphere.center()); \
	if(d < -sphere.radius()) return false; }

	PLANE_TEST(far);
	PLANE_TEST(near);
	PLANE_TEST(left);
	PLANE_TEST(right);
	PLANE_TEST(top);
	PLANE_TEST(bottom);

	return true;
}

} // intersect

} // RGL

