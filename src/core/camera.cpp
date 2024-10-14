#include "camera.h"
#include "window.h"

namespace RGL
{
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
            glm::mat4 R = glm::mat4_cast(m_orientation);
            glm::mat4 T = glm::translate(glm::mat4(1.0f), -m_position);

            m_view = R * T;

            m_is_dirty = false;
        }
    }

    void Camera::move(const glm::vec3& position, const glm::vec3& dir, float amount)
    {
        setPosition(position + (dir * amount));
    }
}
