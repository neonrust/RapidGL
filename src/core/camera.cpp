#include "camera.h"

namespace RGL
{

static constexpr glm::vec3 AXIS_X { 1, 0, 0 };
static constexpr glm::vec3 AXIS_Y { 0, 1, 0 };
static constexpr glm::vec3 AXIS_Z { 0, 0, 1 };

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
	m_direction             (-AXIS_Z),
	m_width                 (0),
	m_height                (0),
	m_near                  (0.01f),
	m_far                   (100),
	m_fovy                  (60),
	m_mouse_pressed_position(glm::vec2(0)),
	m_is_dirty              (true),
	m_is_mouse_move         (false)
{
}

void Camera::setSize(int width, int height)
{
	if(size_t(width) == m_width and size_t(height) == m_height)
		return;

	if(m_is_ortho)
		assert(false);
	else
		setPerspective(m_fovy, width, height, m_near, m_far);
}

void Camera::setPerspective(float fovy, int width, int height, float z_near, float z_far)
{
	m_projection   = glm::perspectiveFov(glm::radians(fovy), float(width), float(height), z_near, z_far);
	m_is_ortho     = false;
	m_width        = size_t(width);
	m_height       = size_t(height);
	m_near         = z_near;
	m_far          = z_far;
	m_fovy         = fovy;
	m_is_dirty     = true;
}

void Camera::setOrtho(float left, float right, float bottom, float top, float z_near, float z_far)
{
	m_projection   = glm::ortho(left, right, bottom, top, z_near, z_far);
	m_is_ortho     = true;
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
	// Camera Movement
	const auto movement_amount = float(m_move_speed * dt);

	if (Input::getKey(m_forward_key))
		// move(m_position, glm::conjugate(m_orientation) * glm::vec3(0, 0, -1), movement_amount);
		move(m_position, forwardVector(), movement_amount);

	if (Input::getKey(m_backward_key))
		// move(m_position, glm::conjugate(m_orientation) * glm::vec3(0, 0, 1), movement_amount);
		move(m_position, -forwardVector(), movement_amount);

	if (Input::getKey(m_right_key))
		// move(m_position, glm::conjugate(m_orientation) * glm::vec3(1, 0, 0), movement_amount);
		move(m_position, rightVector(), movement_amount);

	if (Input::getKey(m_left_key))
		// move(m_position, glm::conjugate(m_orientation) * glm::vec3(-1, 0, 0), movement_amount);
		move(m_position, -rightVector(), movement_amount);

	if (Input::getKey(m_up_key))
		move(m_position, AXIS_Y, movement_amount); // regardless of orientation

	if (Input::getKey(m_down_key))
		move(m_position, -AXIS_Y, movement_amount); // regardless of orientation

	/* Camera Rotation */
	if (Input::getMouse(m_unlock_mouse_key))
	{
		if (not m_is_mouse_move)
		{
			m_mouse_pressed_position = glm::ivec2(Input::getMousePosition());
			Input::setMouseCursorVisibility(false);
			//Input::setMousePosition( center of the screen )
			m_is_mouse_move = true;
		}
	}
	else if(m_is_mouse_move)
	{
		Input::setMousePosition(m_mouse_pressed_position);
		Input::setMouseCursorVisibility(true);
		m_is_mouse_move = false;
	}

	if (m_is_mouse_move)
	{
		auto mouse_pos = glm::ivec2(Input::getMousePosition());
		auto delta_pos = mouse_pos - m_mouse_pressed_position; // TODO: actually, subtract the center of the screen
		Input::setMousePosition(m_mouse_pressed_position); // TODO: actually, to the center of the screen

		// TODO: probably, this should instead keep track of yaw & pitch (as floats)
		//   and here just update those, and at the end build 'm_orientation' from those
		//   and of course, this all should be in a "controller" class, not in the camera itself

		// yaw   (rotation around the Y axis)
		if (delta_pos.x)
		{
			const auto angle = glm::radians(float(delta_pos.x) * m_sensitivity);
			setOrientation(m_orientation * glm::angleAxis(angle, AXIS_Y));
		}

		// pitch  (rotation around the X axis)
		if (delta_pos.y)
		{
			const auto angle = glm::radians(float(delta_pos.y) * m_sensitivity);
			setOrientation(glm::angleAxis(angle, AXIS_X) * m_orientation);
		}
	}

	if (m_is_dirty)
	{
		const auto R = glm::mat4_cast(m_orientation);
		const auto T = glm::translate(glm::mat4(1), -m_position);

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

	updateDirection();
	m_is_dirty  = true;
}

void Camera::setOrientation(const glm::vec3 &direction)
{
	m_orientation = glm::quatLookAt(glm::normalize(direction), glm::vec3(0, 1, 0));
	updateDirection();
	m_is_dirty  = true;
}

void Camera::setOrientation(const glm::vec3 &axis, float angle)
{
	m_orientation = glm::angleAxis(glm::radians(angle), glm::normalize(axis));
	updateDirection();
	m_is_dirty    = true;
}

void Camera::setOrientation(const glm::quat &quat)
{
	m_orientation = quat;
	updateDirection();
	m_is_dirty    = true;
}

void Camera::updateDirection()
{
	m_direction = glm::normalize(glm::conjugate(m_orientation) * AXIS_Z);
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
		setPerspective(fov, m_width, m_height, m_near, m_far);
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

glm::vec3 Camera::upVector() const
{
	return glm::normalize(glm::conjugate(m_orientation) * AXIS_Y);
}

glm::vec3 Camera::rightVector() const
{
	return glm::normalize(glm::conjugate(m_orientation) * AXIS_X);
}

} // RGL
