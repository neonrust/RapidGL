#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp> // glm::decompose
#include <glm/gtx/quaternion.hpp>       // glm::quat_identity


namespace component
{

static constexpr auto ident_quat = glm::quat_identity<float, glm::defaultp>();

struct Transform
{
	inline explicit  Transform(const glm::mat4 &tfm=glm::mat4(1)) :
		_transform(tfm),
		_dirty(false)
	{
		glm::vec3 skew;        // not used
		glm::vec4 perspective; // not used

		glm::decompose(tfm, _scale, _orientation, _position, skew, perspective);
		// TODO: asserts for non-zero skew or perspective?
	}
	inline explicit Transform(const glm::quat &orientation) :
		_position(glm::vec3(0)),
		_orientation(orientation),
		_scale(glm::vec3(1))
	{
	}
	inline explicit Transform(const glm::vec3 &position, const glm::vec3 &scale=glm::vec3(1)) :
		_position(position),
		_orientation(ident_quat),
		_scale(scale)
	{
	}

	inline void set_position   (const glm::vec3 &pos)   { _position = pos;    _dirty = true; }
	inline void set_orientation(const glm::quat &ori)   { _orientation = ori; _dirty = true; }
	inline void set_scale      (const glm::vec3 &scale) { _scale= scale;      _dirty = true; }

	inline glm::vec3 position() const    { return _position; }
	inline glm::quat orientation() const { return _orientation; }
	inline glm::vec3 scale() const       { return _scale; }

	inline operator const glm::mat4 &() const { return transform(); }

	const glm::mat4 &transform() const
	{
		if(_dirty)
		{
			_dirty = false;
			_transform = glm::translate(glm::mat4(1), _position);
			_transform = _transform * glm::mat4_cast(_orientation);
			_transform = glm::scale(_transform, _scale);
		}
		return _transform;
	}

	inline glm::mat3 normal_matrix() const {
		return glm::transpose(glm::inverse(glm::mat3_cast(_orientation)));
	}

	inline float max_scale() const {
		return glm::max(_scale.x, glm::max(_scale.y, _scale.z));
	}

private:
	glm::vec3 _position          { glm::vec3(0) };
	glm::quat _orientation       { ident_quat };
	glm::vec3 _scale             { glm::vec3(1) };
	mutable glm::mat4 _transform { glm::mat4(1) };
	mutable bool _dirty          { true };
};

} // component
