#include "component_transform.h"

namespace RGL::component
{

const glm::vec3 Transform::direction_reference { 0, 0, -1 };

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