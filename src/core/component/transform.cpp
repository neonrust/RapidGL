#include "component/transform.h"

#include "constants.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>


namespace RGL::component
{

const glm::vec3 Transform::direction_reference { 0, 0, -1 };

void Transform::set_orientation_xyz(const glm::vec3 &angles)
{
	const auto m = glm::eulerAngleXYZ(glm::radians(angles.x), glm::radians(angles.y), glm::radians(angles.z));

	set_orientation(glm::quat_cast(m));
}

void Transform::set_orientation_xzy(const glm::vec3 &angles)
{
	const auto m = glm::eulerAngleXZY(glm::radians(angles.x), glm::radians(angles.y), glm::radians(angles.z));

	set_orientation(glm::quat_cast(m));
}

void Transform::set_orientation_yxz(const glm::vec3 &angles)
{
	const auto m = glm::eulerAngleYXZ(glm::radians(angles.x), glm::radians(angles.y), glm::radians(angles.z));

	set_orientation(glm::quat_cast(m));
}

void Transform::set_orientation_yzx(const glm::vec3 &angles)
{
	const auto m = glm::eulerAngleYZX(glm::radians(angles.x), glm::radians(angles.y), glm::radians(angles.z));

	set_orientation(glm::quat_cast(m));
}

void Transform::set_orientation_zxy(const glm::vec3 &angles)
{
	const auto m = glm::eulerAngleZXY(glm::radians(angles.x), glm::radians(angles.y), glm::radians(angles.z));

	set_orientation(glm::quat_cast(m));
}

void Transform::set_orientation_zyx(const glm::vec3 &angles)
{
	const auto m = glm::eulerAngleZYX(glm::radians(angles.x), glm::radians(angles.y), glm::radians(angles.z));

	set_orientation(glm::quat_cast(m));
}

void Transform::set_direction(const glm::vec3 &dir)
{
	set_orientation(glm::rotation(direction_reference, dir));
}

const glm::vec3 RGL::component::Transform::direction() const
{
	return _orientation * glm::vec4(direction_reference, 1);
}

glm::vec3 Transform::orientation_xyz() const
{
	glm::vec3 angles;
	glm::extractEulerAngleXYZ(glm::mat4_cast(orientation()), angles.x, angles.y, angles.z);
	return angles;
}

glm::vec3 Transform::orientation_xzy() const
{
	glm::vec3 angles;
	glm::extractEulerAngleXZY(glm::mat4_cast(orientation()), angles.x, angles.y, angles.z);
	return angles;
}

glm::vec3 Transform::orientation_yxz() const
{
	glm::vec3 angles;
	glm::extractEulerAngleYXZ(glm::mat4_cast(orientation()), angles.x, angles.y, angles.z);
	return angles;
}

glm::vec3 Transform::orientation_yzx() const
{
	glm::vec3 angles;
	glm::extractEulerAngleYZX(glm::mat4_cast(orientation()), angles.x, angles.y, angles.z);
	return angles;
}

glm::vec3 Transform::orientation_zxy() const
{
	glm::vec3 angles;
	glm::extractEulerAngleZXY(glm::mat4_cast(orientation()), angles.x, angles.y, angles.z);
	return angles;
}

glm::vec3 Transform::orientation_zyx() const
{
	glm::vec3 angles;
	glm::extractEulerAngleZYX(glm::mat4_cast(orientation()), angles.x, angles.y, angles.z);
	return angles;
}

const glm::mat4 &Transform::transform() const
{
	if(_matrix_dirty)
	{
		_matrix_dirty = false;
		_transform = glm::translate(glm::mat4(1), _position);
		_transform = _transform * glm::mat4_cast(_orientation);
		_transform = glm::scale(_transform, _scale);
	}
	return _transform;
}

void Transform::look_at(const glm::vec3 &pos, const glm::vec3 &up)
{
	_orientation = glm::quat_cast(glm::lookAt(_position, pos, up));
	_matrix_dirty = true;
}

} // RGL::component