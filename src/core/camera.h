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

class Camera
{
public:
	/*
	 * Perspective camera
	 */
	Camera(float fovy,
		   float aspect_ratio,
		   float z_near,
		   float z_far)
		: Camera()
	{
		m_projection   = glm::perspective(glm::radians(fovy), aspect_ratio, z_near, z_far);
		m_aspect_ratio = aspect_ratio;
		m_near         = z_near;
		m_far          = z_far;
		m_fovy         = fovy;
	}

	/*
	 * Ortho camera
	 */
	Camera(float left,
		   float right,
		   float bottom,
		   float top,
		   float z_near,
		   float z_far)
		: Camera(true)
	{
		m_projection   = glm::ortho(left, right, bottom, top, z_near, z_far);
		m_aspect_ratio = 1.f;
		m_near         = z_near;
		m_far          = z_far;
		m_fovy         = 1.f;
	}
	Camera(bool is_ortho=false);

	void setPosition(const glm::vec3& position)
	{
		m_position = position;
		m_is_dirty = true;
	}

	/*
	 * Set orientation using Euler Angles in degrees
	 */
	void setOrientationEuler(const glm::vec3 &euler)
	{
		m_orientation = glm::angleAxis(glm::radians(euler.x), glm::vec3(1, 0, 0)) *
			glm::angleAxis(glm::radians(euler.y), glm::vec3(0, 1, 0)) *
			glm::angleAxis(glm::radians(euler.z), glm::vec3(0, 0, 1));

		m_direction = glm::normalize(glm::conjugate(m_orientation) * glm::vec3(0, 0, 1));
		m_is_dirty  = true;
	}

	/*
	 * Set orientation using explicit direction vector
	 */
	void setOrientation(const glm::vec3 & direction)
	{
		m_orientation = glm::quatLookAt(glm::normalize(direction), glm::vec3(0, 1, 0));
		m_direction = glm::normalize(glm::conjugate(m_orientation) * glm::vec3(0, 0, 1));
		m_is_dirty  = true;
	}

	/*
	 * Set orientation using axis and angle in degrees
	 */
	void setOrientation(const glm::vec3& axis, float angle)
	{
		m_orientation = glm::angleAxis(glm::radians(angle), glm::normalize(axis));
		m_direction   = glm::normalize(glm::conjugate(m_orientation) * glm::vec3(0, 0, 1));
		m_is_dirty    = true;
	}

	void setOrientation(const glm::quat& quat)
	{
		m_orientation = quat;
		m_direction   = glm::normalize(glm::conjugate(m_orientation) * glm::vec3(0, 0, 1));
		m_is_dirty    = true;
	}

	void setFarPlane(float f)  { m_far = f; m_is_dirty = true; }
	void setNearPlane(float n) { m_near = n; m_is_dirty = true; }

	inline glm::quat orientation() const { return m_orientation; }
	inline glm::vec3 position()    const { return m_position; }
	inline glm::vec3 direction()   const { return m_direction; }
	inline float aspectRatio()     const { return m_aspect_ratio; }
	inline float nearPlane()       const { return m_near; }
	inline float farPlane()        const { return m_far; }
	inline float verticalFov()     const { return m_fovy; }

	const Frustum &frustum();




	// TODO: this shouldn't be here (better in a controller-type thing)
	void update(double dt);

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
	void updateFrustum();

private:
	Frustum _frustum;

	glm::quat m_orientation;
	glm::vec3 m_position;
	glm::vec3 m_direction;
	float m_near;
	float m_far;
	float m_aspect_ratio;
	float m_fovy; // in degrees

	glm::vec2 m_mouse_pressed_position;
	bool      m_is_dirty;
	bool      m_is_mouse_move;

	void move(const glm::vec3 & position, const glm::vec3& dir, float amount);
};

} // RGL
