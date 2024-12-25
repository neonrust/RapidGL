#include "camera.h"

namespace RGL
{

Camera::Camera(bool is_ortho)
	: m_view                  (1),
	m_projection            (1),
	m_is_ortho              (is_ortho),
	m_sensitivity           (0.2f),
	m_move_speed            (10),
	m_unlock_mouse_key      (KeyCode::MouseRight),
	m_forward_key           (KeyCode::W),
	m_backward_key          (KeyCode::S),
	m_left_key              (KeyCode::A),
	m_right_key             (KeyCode::D),
	m_up_key                (KeyCode::E),
	m_down_key              (KeyCode::Q),
	m_orientation           (glm::vec3(0)),
	m_position              (glm::vec3(0)),
	m_direction             (glm::vec3(0, 0, -1)),
	m_near                  (0.01f),
	m_far                   (100),
	m_aspect_ratio          (1),
	m_fovy                  (60),
	m_mouse_pressed_position(glm::vec2(0)),
	m_is_dirty              (true),
	m_is_mouse_move         (false)
{
}

void Camera::setPerspective(float fovy, float aspect_ratio, float z_near, float z_far)
{
	m_projection   = glm::perspective(glm::radians(fovy), aspect_ratio, z_near, z_far);
	m_aspect_ratio = aspect_ratio;
	m_near         = z_near;
	m_far          = z_far;
	m_fovy         = fovy;
	m_is_dirty     = true;
}

void Camera::setOrtho(float left, float right, float bottom, float top, float z_near, float z_far)
{
	m_projection   = glm::ortho(left, right, bottom, top, z_near, z_far);
	m_aspect_ratio = 1.f;
	m_near         = z_near;
	m_far          = z_far;
	m_fovy         = 1.f;
	m_is_dirty     = true;
}

const Frustum &Camera::frustum()
{
	if(m_is_dirty)
		updateFrustum();
	return _frustum;
}

void Camera::update(double dt)
{
	/* Camera Movement */
	auto movement_amount = float(m_move_speed * dt);

	if (Input::getKey(m_forward_key))
		move(m_position, glm::conjugate(m_orientation) * glm::vec3(0, 0, -1), movement_amount);

	if (Input::getKey(m_backward_key))
		move(m_position, glm::conjugate(m_orientation) * glm::vec3(0, 0, 1), movement_amount);

	if (Input::getKey(m_right_key))
		move(m_position, glm::conjugate(m_orientation) * glm::vec3(1, 0, 0), movement_amount);

	if (Input::getKey(m_left_key))
		move(m_position, glm::conjugate(m_orientation) * glm::vec3(-1, 0, 0), movement_amount);

	if (Input::getKey(m_up_key))
		move(m_position, glm::vec3(0, 1, 0), movement_amount);

	if (Input::getKey(m_down_key))
		move(m_position, glm::vec3(0, -1, 0), movement_amount);

	/* Camera Rotation */
	if (Input::getMouse(m_unlock_mouse_key))
	{
		if (!m_is_mouse_move)
			m_mouse_pressed_position = Input::getMousePosition();

		m_is_mouse_move = true;
	}
	else
		m_is_mouse_move = false;

	if (m_is_mouse_move)
	{
		auto mouse_pos = Input::getMousePosition();
		auto delta_pos = mouse_pos - m_mouse_pressed_position;

		auto rot_y = delta_pos.x != 0.0f;
		auto rot_x = delta_pos.y != 0.0f;

		/* pitch */
		if (rot_x)
			setOrientation(glm::angleAxis(glm::radians(delta_pos.y * m_sensitivity), glm::vec3(1, 0, 0)) * m_orientation);

		/* yaw */
		if (rot_y)
			setOrientation(m_orientation * glm::angleAxis(glm::radians(delta_pos.x * m_sensitivity), glm::vec3(0, 1, 0)));

		if (rot_x || rot_y)
			m_mouse_pressed_position = Input::getMousePosition();
	}

	if (m_is_dirty)
	{
		glm::mat4 R = glm::mat4_cast(m_orientation);
		glm::mat4 T = glm::translate(glm::mat4(1), -m_position);

		m_view = R * T;

		updateFrustum();

		m_is_dirty = false;
	}
}

void Camera::updateFrustum()
{
	_frustum.setFromView(m_projection, m_view);
}

void Camera::setPosition(const glm::vec3 &position)
{
	m_position = position;
	m_is_dirty = true;
}

void Camera::setOrientationEuler(const glm::vec3 &euler)
{
	m_orientation = glm::angleAxis(glm::radians(euler.x), glm::vec3(1, 0, 0)) *
		glm::angleAxis(glm::radians(euler.y), glm::vec3(0, 1, 0)) *
		glm::angleAxis(glm::radians(euler.z), glm::vec3(0, 0, 1));

	m_direction = glm::normalize(glm::conjugate(m_orientation) * glm::vec3(0, 0, 1));
	m_is_dirty  = true;
}

void Camera::setOrientation(const glm::vec3 &direction)
{
	m_orientation = glm::quatLookAt(glm::normalize(direction), glm::vec3(0, 1, 0));
	m_direction = glm::normalize(glm::conjugate(m_orientation) * glm::vec3(0, 0, 1));
	m_is_dirty  = true;
}

void Camera::setOrientation(const glm::vec3 &axis, float angle)
{
	m_orientation = glm::angleAxis(glm::radians(angle), glm::normalize(axis));
	m_direction   = glm::normalize(glm::conjugate(m_orientation) * glm::vec3(0, 0, 1));
	m_is_dirty    = true;
}

void Camera::setOrientation(const glm::quat &quat)
{
	m_orientation = quat;
	m_direction   = glm::normalize(glm::conjugate(m_orientation) * glm::vec3(0, 0, 1));
	m_is_dirty    = true;
}

void Camera::move(const glm::vec3& position, const glm::vec3& dir, float amount)
{
	setPosition(position + (dir * amount));
}

void Camera::setFov(float fov)
{
	if(m_is_ortho)
	{
		std::fprintf(stderr, "Camera: Ignored FOV setting in ORTHO mode\n");
		return;
	}

	if(auto changed = fov != m_fovy; changed)
		setPerspective(fov, m_aspect_ratio, m_near, m_far);
}

void RGL::Camera::setFarPlane(float f)
{
	m_is_dirty = f != m_far;
	m_far = f;
}

void Camera::setNearPlane(float n)
{
	m_is_dirty = n != m_near;
	m_near = n;
}

} // RGL
