#include "camera.h"

#include "log.h"
#include "shader.h" // IWYU pragma: keep
#include "constants.h"
#include "hash_combine.h"
#include "hash_vec3.h"   // IWYU pragma: keep
#include "hash_mat4.h"   // IWYU pragma: keep
#include "hash_quat.h"   // IWYU pragma: keep

#include <chrono>
using namespace std::chrono;

#include "game_time.h"

using namespace std::literals;


namespace RGL
{

Camera::Camera(bool is_ortho)
	: _view                  (1),
	m_projection            (1),
	_is_ortho              (is_ortho),
	m_sensitivity           (0.2f),
	m_move_speed            (10),
	m_unlock_mouse_button   (MouseButton::Right),
	m_forward_key           (KeyCode::W),
	m_backward_key          (KeyCode::S),
	m_left_key              (KeyCode::A),
	m_right_key             (KeyCode::D),
	m_up_key                (KeyCode::E),
	m_down_key              (KeyCode::Q),
	_yaw                    (0),
	_pitch                  (0),
	// m_orientation           (glm::vec3(0)),
	// m_position              (glm::vec3(0)),
	// m_direction             (-AXIS_Z),
	_width                 (0),
	_height                (0),
	_near                  (0.1f),
	_far                   (200),
	_fovy                  (60),
	_mouse_pressed_position(glm::vec2(0)),
	_is_dirty              (true),
	_is_mouse_move         (false)
{
}

void Camera::setSize(size_t width, size_t height)
{
	if(width == _width and height == _height)
		return;

	if(_is_ortho)
		assert(false);
	else
		setPerspective(_fovy, width, height, _near, _far);
}

void Camera::setPerspective(float fovy, size_t width, size_t height, float z_near, float z_far)
{
	_is_ortho     = false;
	_width        = width;
	_height       = height;
	_near         = z_near;
	_far          = z_far;
	_fovy         = fovy;
	m_projection   = projectionTransform(z_near, z_far);
	_is_dirty     = true;
}

void Camera::setOrtho(float left, float right, float bottom, float top, float z_near, float z_far)
{
	_is_ortho     = true;
	_left         = left;
	_right        = right;
	_top          = top;
	_bottom       = bottom;
	_near         = z_near;
	_far          = z_far;
	_fovy         = 1.f;
	m_projection   = projectionTransform(z_near, z_far);
	_is_dirty     = true;
}

const Frustum &Camera::frustum() const
{
	if(_is_dirty)
		const_cast<Camera *>(this)->updateFrustum();
	return _frustum;
}

void Camera::lookAt(const glm::vec3 &point)
{
	const auto direction = glm::normalize(point - position());

	// deduce '_pitch' and '_yaw' from direction vector
	_yaw = std::atan2(direction.x, direction.z);
	_pitch = std::atan2(direction.y, std::sqrt(direction.x*direction.x + direction.z*direction.z));

	_is_dirty = true;
}

glm::mat4 Camera::projectionTransform(float z_near, float z_far) const
{
	if(_is_ortho)
		return glm::ortho(_left, _right, _bottom, _top, z_near, z_far);
	return glm::perspectiveFov(glm::radians(_fovy), float(_width), float(_height), z_near, z_far);
}

void Camera::setUniforms(Shader &shader) const
{
	// TODO: cache locations per shader?  (the shader itself already does caching)
	// TODO: use UBO ?
	// TODO: arguably this could be: shader.setUniforms(camera)

	shader.setUniform("u_cam_pos"sv,         position());
	shader.setUniform("u_projection"sv,      projectionTransform()); // not used ?
	shader.setUniform("u_inv_projection"sv,  glm::inverse(projectionTransform()));
	shader.setUniform("u_view"sv,            viewTransform());       // not used ?
	shader.setUniform("u_inv_view"sv,        glm::inverse(viewTransform()));
	shader.setUniform("u_near_z"sv,          nearPlane());
	shader.setUniform("u_far_z"sv,           farPlane());
	shader.setUniform("u_viewport_size"sv,   viewportSize());
	shader.setUniform("u_fov_y"sv,           glm::radians(_fovy));
}

void Camera::update(std::chrono::nanoseconds dt)
{
	// Camera Movement
	auto movement_amount = m_move_speed * duration_cast<seconds_f>(dt).count();

	if(Input::isKeyDown(KeyCode::LeftShift))
		movement_amount *= 0.1f;
	else if(Input::isKeyDown(KeyCode::LeftControl) or Input::isMouseDown(MouseButton::Mouse4))
		movement_amount *= 5;


	if (Input::isKeyDown(m_forward_key))
		// move(m_position, glm::conjugate(m_orientation) * glm::vec3(0, 0, -1), movement_amount);
		move(_transform.position(), forwardVector(), movement_amount);

	if (Input::isKeyDown(m_backward_key))
		// move(m_position, glm::conjugate(m_orientation) * glm::vec3(0, 0, 1), movement_amount);
		move(_transform.position(), -forwardVector(), movement_amount);

	if (Input::isKeyDown(m_right_key))
		// move(m_position, glm::conjugate(m_orientation) * glm::vec3(1, 0, 0), movement_amount);
		move(_transform.position(), rightVector(), movement_amount);

	if (Input::isKeyDown(m_left_key))
		// move(m_position, glm::conjugate(m_orientation) * glm::vec3(-1, 0, 0), movement_amount);
		move(_transform.position(), -rightVector(), movement_amount);

	if (Input::isKeyDown(m_up_key))
		move(_transform.position(), AXIS_Y, movement_amount); // regardless of orientation

	if (Input::isKeyDown(m_down_key))
		move(_transform.position(), -AXIS_Y, movement_amount); // regardless of orientation

	// Camera Rotation
	if (Input::isMouseDown(m_unlock_mouse_button))
	{
		if (not _is_mouse_move)
		{
			_mouse_pressed_position = glm::ivec2(Input::mousePosition());
			// TODO: see raylib's EnableCursor()/DisableCursor()
			//   also: https://www.glfw.org/docs/3.1/input.html#input_cursor_mode
			Input::setMouseCursorVisible(false);
			Input::setMousePosition({ _width/2, _height/2 });
			_is_mouse_move = true;
		}
	}
	else if(_is_mouse_move)
	{
		Input::setMousePosition(_mouse_pressed_position);
		Input::setMouseCursorVisible(true);
		_is_mouse_move = false;
	}

	if(_is_mouse_move)
	{
		const auto mouse_pos = glm::ivec2(Input::mousePosition());
		const auto delta_pos = mouse_pos - glm::ivec2{ _width/2, _height/2 };
		Input::setMousePosition({ _width/2, _height/2 });

		// TODO: This should instead keep track of yaw & pitch (as floats)
		//   and here just update those, and at the end build 'm_orientation' from those
		//   Also, this all should be in a "controller" class, not in the camera itself.

		// yaw   (rotation around the Y axis)
		if(delta_pos.x)
		{
			addYaw(glm::radians(float(delta_pos.x) * m_sensitivity));
			// setOrientation(m_orientation * glm::angleAxis(angle, AXIS_Y));
		}

		// pitch  (rotation around the X axis)
		if(delta_pos.y)
		{
			addPitch(glm::radians(float(delta_pos.y) * m_sensitivity));
			// setOrientation(glm::angleAxis(angle, AXIS_X) * m_orientation);
		}
	}

	if(_is_dirty)
	{
		_transform.set_orientation(glm::angleAxis(_pitch, AXIS_X) * glm::angleAxis(_yaw, AXIS_Y));

		updateView();
		updateDirection();
		updateFrustum();

		_is_dirty = false;
	}
}

size_t Camera::hash() const
{
	size_t h { 0 };
	h = hash_combine(h, _transform.position());
	h = hash_combine(h, _transform.orientation());
	h = hash_combine(h, m_projection);
	return h;
}

void Camera::updateFrustum()
{
	_frustum.setFromView(m_projection, _view, _transform.position(), forwardVector());
}

void Camera::setPosition(const glm::vec3 &position)
{
	_transform.set_position(position);
	_is_dirty = true;
}

void Camera::setOrientationEuler(const glm::vec3 &euler)
{
	_transform.set_orientation(glm::angleAxis(glm::radians(euler.x), AXIS_X) *
		glm::angleAxis(glm::radians(euler.y), AXIS_Y) *
		glm::angleAxis(glm::radians(euler.z), AXIS_Z));

	_yaw = glm::radians(euler.y);
	_pitch = glm::radians(euler.x);
	// to apply limits
	addYaw(0);
	addPitch(0);

	updateDirection();
	_is_dirty  = true;
}

void Camera::setOrientation(const glm::vec3 &direction)
{
	// TODO: _yaw, _pitch
	_transform.set_orientation(glm::quatLookAt(glm::normalize(direction), AXIS_Y));
	_yaw = std::acos(glm::dot(glm::normalize(glm::vec3{direction.x, 0, direction.z}), AXIS_Z));
	_pitch = std::acos(glm::dot(glm::normalize(glm::vec3{0, direction.y, direction.z}), AXIS_Z));
	updateDirection();
	_is_dirty = true;
}

void Camera::setOrientation(const glm::vec3 &axis, float angle)
{
	_transform.set_orientation(glm::angleAxis(glm::radians(angle), glm::normalize(axis)));
	// TODO: _yaw, _pitch
	updateDirection();
	_is_dirty = true;
}

void Camera::setOrientation(const glm::quat &quat)
{
	_transform.set_orientation(quat);
	// TODO: _yaw, _pitch
	updateDirection();
	_is_dirty = true;
}

void Camera::updateView()
{
	const auto R = glm::mat4_cast(_transform.orientation());
	const auto T = glm::translate(glm::mat4(1), -_transform.position());

	_view = R * T;
}

void Camera::updateDirection()
{
	_direction = glm::normalize(glm::conjugate(_transform.orientation()) * AXIS_Z);
}

void Camera::move(const glm::vec3& position, const glm::vec3& dir, float amount)
{
	setPosition(position + (dir * amount));
}

void Camera::addYaw(float angle)
{
	_is_dirty = angle != 0;
	_yaw += angle;

	if(_yaw > std::numbers::pi_v<float>)
		_yaw -= std::numbers::pi_v<float>*2;
	else if(_yaw < -std::numbers::pi_v<float>)
		_yaw += std::numbers::pi_v<float>*2;
}

void Camera::addPitch(float angle)
{
	_is_dirty = angle != 0;
	_pitch = glm::clamp(_pitch + angle, -std::numbers::pi_v<float>/2, std::numbers::pi_v<float>/2);
}

void Camera::setFov(float fov)
{
	if(_is_ortho)
	{
		Log::warning("Camera: Ignored FOV setting in ORTHO mode\n");
		return;
	}

	if(fov != _fovy)
		setPerspective(fov, _width, _height, _near, _far);
}

float Camera::horizontalFov() const
{
	return glm::degrees(2.f * std::atan(std::tan(glm::radians(_fovy) * 0.5f) * aspectRatio()));
}

void RGL::Camera::setFarPlane(float f)
{
	_is_dirty = f != _far;
	_far = f;
}

void Camera::setNearPlane(float n)
{
	_is_dirty = n != _near;
	_near = n;
}

glm::vec3 Camera::upVector() const
{
	return glm::normalize(glm::conjugate(_transform.orientation()) * AXIS_Y);
}

glm::vec3 Camera::rightVector() const
{
	return glm::normalize(glm::conjugate(_transform.orientation()) * AXIS_X);
}

} // RGL
