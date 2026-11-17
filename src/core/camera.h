#pragma once
#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "component/transform.h"
#include "frustum.h"
#include "input.h"
#include "input_bind.h"


namespace RGL
{

class Shader;

class Camera
{
public:
	/*
	 * Perspective camera
	 */
	Camera(float fovy, float z_near, float z_far) : Camera()
	{
		// should/must call  setSize() after creating the camera
		setPerspective(fovy, 1280, 720, z_near, z_far);
	}

	/*
	 * Ortho camera
	 */
	Camera(float left, float right, float bottom, float top, float z_near, float z_far) : Camera(true)
	{
		setOrtho(left, right, bottom, top, z_near, z_far);
	}

	Camera(bool is_ortho=false);
	
	
	void setSize(size_t width, size_t height);
	void setPerspective(float fovy, size_t width, size_t height, float z_near, float z_far);
	void setOrtho(float left, float right, float bottom, float top, float z_near, float z_far);


	void setPosition(const glm::vec3& position);

	// Set orientation using Euler Angles in degrees
	void setOrientationEuler(const glm::vec3 &euler);
	// Set orientation using explicit direction vector
	void setOrientation(const glm::vec3 & direction);
	// Set orientation using axis and angle in degrees
	void setOrientation(const glm::vec3& axis, float angle);
	void setOrientation(const glm::quat& quat);

	inline float yaw() const { return _yaw; }
	inline float pitch() const { return _pitch; }

	// set/get vertical fov (degrees)
	void setFov(float fov);
	inline float verticalFov() const { return _fovy; }
	float horizontalFov() const;

	void setFarPlane(float f);
	void setNearPlane(float n);

	inline glm::vec3 position()    const { return _transform.position(); }
	inline glm::quat orientation() const { return _transform.orientation(); }

	inline glm::vec3 directionVector() const { return _direction; }
	inline glm::vec3 forwardVector()   const { return -_direction; }
		   glm::vec3 rightVector()     const;
		   glm::vec3 upVector()        const;
	inline float aspectRatio()   const { return float(_width) / float(_height); }
	inline float nearPlane()     const { return _near; }
	inline float farPlane()      const { return _far; }
	
	const Frustum &frustum() const;

	void lookAt(const glm::vec3 &point);

	inline const glm::mat4 &projectionTransform() const { return m_projection; }
	inline const glm::mat4 &viewTransform() const { return _view; }

	// create same projection as current, except different near & far planes
	glm::mat4 projectionTransform(float z_near, float z_far) const;

	void setUniforms(Shader &shader) const;

	// TODO: this shouldn't be here (better in a controller-type thing)
	void update(std::chrono::nanoseconds dt);

	inline glm::uvec2 viewportSize() const { return { _width, _height }; }

	inline float brightness() const { return _brightness; }
	inline void setBrightness(float brightness) { _brightness = brightness; }

	size_t hash() const;

private:
	glm::mat4 _view;
	glm::mat4 m_projection;
	bool _is_ortho;

	float m_sensitivity;
	float m_move_speed;

	ActionBind _move_slow_bind;
	ActionBind _move_fast_bind1;
	ActionBind _move_fast_bind2;
	ActionBind _forward_bind;
	ActionBind _backward_bind;
	ActionBind _left_bind;
	ActionBind _right_bind;
	ActionBind _up_bind;
	ActionBind _down_bind;
	ActionBind _freelook_bind;
	AxisBind2d _freelook_axis;

private:
	void updateView();
	void updateDirection();
	void updateFrustum();
	void move(const glm::vec3 & position, const glm::vec3& dir, float amount);
	void setYaw(float angle);
	void setPitch(float angle);

private:
	float _yaw;
	float _pitch;
	Frustum _frustum;
	float _brightness { 0 };

	component::Transform _transform;
	// glm::quat m_orientation;
	// glm::vec3 m_position;
	glm::vec3 _direction;
	size_t _width;
	size_t _height;
	float _near;
	float _far;
	float _left;
	float _right;
	float _top;
	float _bottom;
	float _fovy; // in degrees

	bool  _is_dirty;
};

} // RGL
