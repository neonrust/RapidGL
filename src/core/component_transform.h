#pragma once

// #include "log.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp> // glm::decompose
#include <glm/gtx/quaternion.hpp>       // glm::quat_identity


namespace RGL::component
{

static constexpr auto ident_quat = glm::quat_identity<float, glm::defaultp>();

struct Transform
{
	static const glm::vec3 direction_reference;

	// from existing transform matrix -> decompose
	inline explicit  Transform(const glm::mat4 &tfm) :
		_transform(tfm),
		_matrix_dirty(false)
	{
		// Log::debug("Transform (tfm)");
		glm::vec3 skew;        // not used
		glm::vec4 perspective; // not used

		glm::decompose(tfm, _scale, _orientation, _position, skew, perspective);
		// TODO: asserts for non-zero skew or perspective?
	}
	// from only position
	inline explicit Transform(const glm::vec3 &position) :
		_position(position)
	{
		// Log::debug("Transform (pos)");
	}
	// from only orientation
	inline explicit Transform(const glm::quat &orientation) :
		_orientation(orientation)
	{
		// Log::debug("Transform (ori)");
	}
	// from position, orientation & scale
	inline explicit Transform(const glm::vec3 &position, const glm::quat &orientation, const glm::vec3 &scale=glm::vec3(1)) :
		_position(position),
		_orientation(orientation),
		_scale(scale)
	{
		// Log::debug("Transform (pos,ori,scale)");
	}
	// from only direction (set only orientation)
	struct Direction {}; // to disambiguate 'direction' argument'
	inline explicit  Transform(Direction, const glm::vec3 &direction) :
		_matrix_dirty(true)
	{
		// Log::debug("Transform (dir)");
		set_direction(direction);
	}
	// from position & direction
	inline explicit  Transform(const glm::vec3 &position, Direction, const glm::vec3 &direction) :
		_position(position)
	{
		// Log::debug("Transform (pos, dir)");
		set_direction(direction);
	}
	// from position & scale
	struct Scale {}; // to disambiguate 'scale' argument'
	inline explicit Transform(const glm::vec3 &position, Scale, const glm::vec3 &scale) :
		_position(position),
		_scale(scale)
	{
		// Log::debug("Transform (scale)");
	}

	inline void set_position   (const glm::vec3 &pos)   { _position = pos;    _matrix_dirty = true; }
	inline void set_orientation(const glm::quat &ori)   { _orientation = ori; _matrix_dirty = true; }
	inline void set_scale      (const glm::vec3 &scale) { _scale= scale;      _matrix_dirty = true; }
		   void set_direction  (const glm::vec3 &dir);

	inline const glm::vec3 &position() const    { return _position; }
	inline const glm::quat &orientation() const { return _orientation; }
	inline const glm::vec3 &scale() const       { return _scale; }
		   const glm::vec3  direction() const;

	inline void move(const glm::vec3 &delta) { _position += delta; _matrix_dirty = true; }

	inline operator const glm::mat4 &() const { return transform(); }

	const glm::mat4 &transform() const;

	inline glm::mat3 normal_matrix() const {
		return glm::transpose(glm::inverse(glm::mat3_cast(_orientation)));
	}

	inline float max_scale() const {
		return glm::max(_scale.x, glm::max(_scale.y, _scale.z));
	}

	void look_at(const glm::vec3 &pos, const glm::vec3 &up=glm::vec3(0, 1, 0));

private:
	glm::vec3 _position          { glm::vec3(0) };
	glm::quat _orientation       { ident_quat };
	glm::vec3 _scale             { glm::vec3(1) };

	mutable glm::mat4 _transform { glm::mat4(1) };
	mutable bool _matrix_dirty   { true };
};

} // RGL::component


#include "hash_combine.h"
#include "hash_vec3.h"   // IWYU pragma: keep
#include "hash_vec4.h"   // IWYU pragma: keep

namespace std
{
template<>
struct hash<RGL::component::Transform>
{
	[[nodiscard]] inline size_t operator()(const RGL::component::Transform &t) const
	{
		size_t h { 0 };
		h = hash_combine(h, t.position());
		const auto &o = t.orientation();
		h = hash_combine(h, glm::vec4{ o.x, o.y, o.z, o.w });
		h = hash_combine(h, t.scale());
		return h;
	}
};

} // std
