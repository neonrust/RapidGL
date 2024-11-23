#include "camera.h"
#include "window.h"

namespace RGL
{
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
            {
                m_mouse_pressed_position = Input::getMousePosition();
            }

            m_is_mouse_move = true;
        }
        else
        {
            m_is_mouse_move = false;
        }

        if (m_is_mouse_move)
        {
            auto mouse_pos = Input::getMousePosition();
            auto delta_pos = mouse_pos - m_mouse_pressed_position;

            auto rot_y = delta_pos.x != 0.0f;
            auto rot_x = delta_pos.y != 0.0f;

            /* pitch */
            if (rot_x)
            {
                setOrientation(glm::angleAxis(glm::radians(delta_pos.y * m_sensitivity), glm::vec3(1, 0, 0)) * m_orientation);
            }

            /* yaw */
            if (rot_y)
            {
                setOrientation(m_orientation * glm::angleAxis(glm::radians(delta_pos.x * m_sensitivity), glm::vec3(0, 1, 0)));
            }

            if (rot_x || rot_y)
            {
                m_mouse_pressed_position = Input::getMousePosition();
            }
        }

        if (m_is_dirty)
        {
			static auto printed = false;
            glm::mat4 R = glm::mat4_cast(m_orientation);
			glm::mat4 T = glm::translate(glm::mat4(1), -m_position);
			if(not printed)
			{
				std::printf("  position:  { %.2f; %.2f; %.2f }\n", m_position.x, m_position.y, m_position.z);
				std::printf("  direction: { %.2f; %.2f; %.2f }\n", m_direction.x, m_direction.y, m_direction.z);
				printed = true;
			}


            m_view = R * T;

			updateFrustum();

            m_is_dirty = false;
        }
    }

	void Camera::updateFrustum()
	{
		_frustum.setFromView(m_projection, m_view);
	}

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

    void Camera::move(const glm::vec3& position, const glm::vec3& dir, float amount)
    {
        setPosition(position + (dir * amount));
    }
}
