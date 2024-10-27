#include "bounds.h"
#include "plane.h"

#include <glm/fwd.hpp>

namespace RGL
{

struct Frustum
{
	inline Frustum() {}

	void setFromView(const glm::mat4 &mvp);

	inline const Plane &right() const  { return _right; }
	inline const Plane &left() const   { return _left; }
	inline const Plane &top() const    { return _top; }
	inline const Plane &bottom() const { return _bottom; }
	inline const Plane &back() const   { return _back; }
	inline const Plane &front() const  { return _front; }

	inline const bounds::AABB &aabb() const { return _aabb; }

private:
	Plane _right;
	Plane _left;

	Plane _top;
	Plane _bottom;

	Plane _back;
	Plane _front;

	bounds::AABB _aabb;
};

namespace intersect
{

bool check(const Frustum &f, const bounds::AABB &box);
bool check(const Frustum &f, const glm::vec3 &point);

}

}
