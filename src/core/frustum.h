#include "bounds.h"
#include "plane.h"

#include <glm/fwd.hpp>

namespace RGL
{

struct Frustum
{
	inline Frustum() {}

	void setFromProjection(const glm::mat4 &proj);
	void setFromView(const glm::mat4 &proj, const glm::mat4 &view);

	inline const Plane &right() const  { return _right; }
	inline const Plane &left() const   { return _left; }
	inline const Plane &top() const    { return _top; }
	inline const Plane &bottom() const { return _bottom; }
	inline const Plane &front() const  { return _front; } // a.k.a. near
	inline const Plane &back() const   { return _back; }  // a.k.a. far

	//! world-space AABB
	inline const bounds::AABB &aabb() const { return _aabb; }

	//! world-space corners of frustum volume
	inline const std::array<glm::vec3, 8> corners() const { return _corners; }

private:
	Plane _right;
	Plane _left;
	Plane _top;
	Plane _bottom;
	Plane _front;   // a.k.a. near
	Plane _back;    // a.k.a. far

	std::array<glm::vec3, 8> _corners;

	bounds::AABB _aabb;
};

namespace intersect
{

struct frustum_cull_result
{
	static constexpr auto UNUSED = std::numeric_limits<float>::min();

	inline frustum_cull_result() :
		visible(false),
		culled_by_aabb(false),
		culled_by_plane(-1)
	{
		distance_to_plane[0] = UNUSED;
		distance_to_plane[1] = UNUSED;
		distance_to_plane[2] = UNUSED;
		distance_to_plane[3] = UNUSED;
		distance_to_plane[4] = UNUSED;
		distance_to_plane[5] = UNUSED;
	}

	bool visible { false };

	inline operator bool () const { return visible; }

	bool culled_by_aabb { false };
	std::int_fast8_t culled_by_plane;
	std::array<float, 6> distance_to_plane;
};

frustum_cull_result check(const Frustum &f, const bounds::AABB &box, const glm::mat4 &box_transform);
bool check(const Frustum &f, const glm::vec3 &point);

}

}
