#pragma once
#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "frustum.h"
#include "input.h"

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
	inline float verticalFov() const { return m_fovy; }
	float horizontalFov() const;

	void setFarPlane(float f);
	void setNearPlane(float n);

	inline glm::vec3 position()    const { return m_position; }
	inline glm::quat orientation() const { return m_orientation; }

	inline glm::vec3 directionVector() const { return m_direction; }
	inline glm::vec3 forwardVector()   const { return -m_direction; }
		   glm::vec3 rightVector()     const;
		   glm::vec3 upVector()        const;
	inline float aspectRatio()   const { return float(m_width) / float(m_height); }
	inline float nearPlane()     const { return m_near; }
	inline float farPlane()      const { return m_far; }
	
	const Frustum &frustum() const;

	inline const glm::mat4 &projectionTransform() const { return m_projection; }
	inline const glm::mat4 &viewTransform() const { return m_view; }


	void setUniforms(Shader &shader) const;

	// TODO: this shouldn't be here (better in a controller-type thing)
	void update(double dt);

	inline glm::uvec2 viewportSize() const { return { m_width, m_height }; }

	size_t hash() const;

private:
	glm::mat4 m_view;
	glm::mat4 m_projection;
	bool m_is_ortho;

	float m_sensitivity;
	float m_move_speed;

	KeyCode m_unlock_mouse_key;
	KeyCode m_forward_key;
	KeyCode m_backward_key;
	KeyCode m_left_key;
	KeyCode m_right_key;
	KeyCode m_up_key;
	KeyCode m_down_key;

private:
	void updateDirection();
	void updateFrustum();
	void move(const glm::vec3 & position, const glm::vec3& dir, float amount);
	void addYaw(float angle);
	void addPitch(float angle);

private:
	float _yaw;
	float _pitch;
	Frustum _frustum;

	glm::quat m_orientation;
	glm::vec3 m_position;
	glm::vec3 m_direction;
	size_t m_width;
	size_t m_height;
	float m_near;
	float m_far;
	float m_fovy; // in degrees

	glm::ivec2 m_mouse_pressed_position;
	bool       m_is_dirty;
	bool       m_is_mouse_move;

};

} // RGL
