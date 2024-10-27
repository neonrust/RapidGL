#include "frustum.h"
#include <glm/mat4x4.hpp>
#include "bounds.h"

namespace RGL
{

void Frustum::setFromView(const glm::mat4 &mvp)
{
	// https://www.gamedevs.org/uploads/fast-extraction-viewing-frustum-planes-from-world-view-projection-matrix.pdf

	const glm::vec3 anchor { mvp[0][3], mvp[1][3], mvp[2][3] };

	glm::vec3 p;

	// extract the RIGHT & LEFT planes
	p = glm::vec3{ mvp[0][0], mvp[1][0], mvp[2][0] };
	_right.set(anchor - p, mvp[3][3] - mvp[3][0]);
	_left.set( anchor + p, mvp[3][3] + mvp[3][0]);

	// extract the TOP & BOTTOM planes
	p = glm::vec3{ mvp[0][1], mvp[1][1], mvp[2][1] };
	_top.set(   anchor - p, mvp[3][3] - mvp[3][1]);
	_bottom.set(anchor + p, mvp[3][3] + mvp[3][1]);

	// extract the BACK & FRONT planes
	p = glm::vec3{ mvp[0][2], mvp[1][2], mvp[2][2] };
	_back.set( anchor - p, mvp[3][3] - mvp[3][2]);
	_front.set(anchor + p, mvp[3][3] + mvp[3][2]);


	// build an AABB around the frustum for even faster early-outs by using separating axis test only
	_aabb.clear();

	glm::vec3 corners[8];
	intersect::check(_left,  _top,    _front, &corners[0]);
	intersect::check(_left,  _bottom, _front, &corners[1]);
	intersect::check(_left,  _top,    _back,  &corners[2]);
	intersect::check(_left,  _bottom, _back,  &corners[3]);
	intersect::check(_right, _top,    _front, &corners[0]);
	intersect::check(_right, _bottom, _front, &corners[1]);
	intersect::check(_right, _top,    _back,  &corners[2]);
	intersect::check(_right, _bottom, _back,  &corners[3]);

	for(const auto &corner: corners)
		_aabb.expand(corner);
}

bool intersect::check(const Frustum &f, const bounds::AABB &box)
{
	// TODO: check if any of the 8 corners of the box is contained by the frustum
	return false;
}

bool intersect::check(const Frustum &f, const glm::vec3 &point)
{
	if(not intersect::check(f.aabb(), point))
		return false;

	// true if the 'point' is "in front of"" all planes
	return math::facing(f.back(), point) and math::facing(f.front(), point)
		and math::facing(f.left(), point) and math::facing(f.right(), point)
		and math::facing(f.top(), point) and math::facing(f.bottom(), point);
}

} // RGL
