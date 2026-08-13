#include "component/transform.h"

#include "constants.h"

#include <glm/gtc/matrix_transform.hpp>


namespace RGL::component
{

const glm::vec3 Transform::direction_reference { 0, 0, -1 };

void Transform::set_orientation_xyz(const glm::vec3 &angles)
{
	auto m = \
		glm::angleAxis(glm::radians(angles.x), AXIS_X) *
		glm::angleAxis(glm::radians(angles.y), AXIS_Y) *
		glm::angleAxis(glm::radians(angles.z), AXIS_Z);

	set_orientation(glm::quat_cast(glm::mat3(m)));
}

void Transform::set_orientation_xzy(const glm::vec3 &angles)
{
	auto m = \
		glm::angleAxis(glm::radians(angles.x), AXIS_X) *
		glm::angleAxis(glm::radians(angles.z), AXIS_Z) *
		glm::angleAxis(glm::radians(angles.y), AXIS_Y);

	set_orientation(glm::quat_cast(glm::mat3(m)));
}

void Transform::set_orientation_yxz(const glm::vec3 &angles)
{
	auto m = \
		glm::angleAxis(glm::radians(angles.y), AXIS_Y) *
		glm::angleAxis(glm::radians(angles.x), AXIS_X) *
		glm::angleAxis(glm::radians(angles.z), AXIS_Z);

	set_orientation(glm::quat_cast(glm::mat3(m)));
}

void Transform::set_orientation_yzx(const glm::vec3 &angles)
{
	auto m = \
		glm::angleAxis(glm::radians(angles.y), AXIS_Y) *
		glm::angleAxis(glm::radians(angles.z), AXIS_Z) *
		glm::angleAxis(glm::radians(angles.x), AXIS_X);

	set_orientation(glm::quat_cast(glm::mat3(m)));
}

void Transform::set_orientation_zxy(const glm::vec3 &angles)
{
	auto m = \
		glm::angleAxis(glm::radians(angles.z), AXIS_Z) *
		glm::angleAxis(glm::radians(angles.x), AXIS_X) *
		glm::angleAxis(glm::radians(angles.y), AXIS_Y);

	set_orientation(glm::quat_cast(glm::mat3(m)));
}

void Transform::set_orientation_zyx(const glm::vec3 &angles)
{
	auto m = \
		glm::angleAxis(glm::radians(angles.z), AXIS_Z) *
		glm::angleAxis(glm::radians(angles.y), AXIS_Y) *
		glm::angleAxis(glm::radians(angles.x), AXIS_X);

	set_orientation(glm::quat_cast(glm::mat3(m)));
}

void Transform::set_direction(const glm::vec3 &dir)
{
	set_orientation(glm::rotation(direction_reference, dir));
}

const glm::vec3 RGL::component::Transform::direction() const
{
	return _orientation * glm::vec4(direction_reference, 1);
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