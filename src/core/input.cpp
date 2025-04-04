#include "input.h"

namespace RGL
{
    GLFWwindow * Input::m_window = nullptr;

	dense_map<KeyCode, bool> Input::m_last_keys_states = {
        { KeyCode::Backspace,      false },
        { KeyCode::Delete,         false },
        { KeyCode::Tab,            false },
        { KeyCode::Return,         false },
        { KeyCode::Pause,          false },
        { KeyCode::Escape,         false },
        { KeyCode::Space,          false },
        { KeyCode::Keypad0,        false },
        { KeyCode::Keypad1,        false },
        { KeyCode::Keypad2,        false },
        { KeyCode::Keypad3,        false },
        { KeyCode::Keypad4,        false },
        { KeyCode::Keypad5,        false },
        { KeyCode::Keypad6,        false },
        { KeyCode::Keypad7,        false },
        { KeyCode::Keypad8,        false },
        { KeyCode::Keypad9,        false },
        { KeyCode::KeypadPeriod,   false },
        { KeyCode::KeypadDivide,   false },
        { KeyCode::KeypadMultiply, false },
        { KeyCode::KeypadMinus,    false },
        { KeyCode::KeypadPlus,     false },
        { KeyCode::KeypadEnter,    false },
        { KeyCode::KeypadEquals,   false },
        { KeyCode::UpArrow,        false },
        { KeyCode::DownArrow,      false },
        { KeyCode::RightArrow,     false },
        { KeyCode::LeftArrow,      false },
        { KeyCode::Insert,         false },
        { KeyCode::Home,           false },
        { KeyCode::End,            false },
        { KeyCode::PageUp,         false },
        { KeyCode::PageDown,       false },
        { KeyCode::F1,             false },
        { KeyCode::F2,             false },
        { KeyCode::F3,             false },
        { KeyCode::F4,             false },
        { KeyCode::F5,             false },
        { KeyCode::F6,             false },
        { KeyCode::F7,             false },
        { KeyCode::F8,             false },
        { KeyCode::F9,             false },
        { KeyCode::F10,            false },
        { KeyCode::F11,            false },
        { KeyCode::F12,            false },
        { KeyCode::F13,            false },
        { KeyCode::F14,            false },
        { KeyCode::F15,            false },
        { KeyCode::Alpha0,         false },
        { KeyCode::Alpha1,         false },
        { KeyCode::Alpha2,         false },
        { KeyCode::Alpha3,         false },
        { KeyCode::Alpha4,         false },
        { KeyCode::Alpha5,         false },
        { KeyCode::Alpha6,         false },
        { KeyCode::Alpha7,         false },
        { KeyCode::Alpha8,         false },
        { KeyCode::Alpha9,         false },
        { KeyCode::Comma,          false },
        { KeyCode::Minus,          false },
        { KeyCode::Period,         false },
        { KeyCode::Slash,          false },
        { KeyCode::Semicolon,      false },
        { KeyCode::Equals,         false },
        { KeyCode::LeftBracket,    false },
        { KeyCode::RightBracket,   false },
        { KeyCode::Backslash,      false },
        { KeyCode::A,              false },
        { KeyCode::B,              false },
        { KeyCode::C,              false },
        { KeyCode::D,              false },
        { KeyCode::E,              false },
        { KeyCode::F,              false },
        { KeyCode::G,              false },
        { KeyCode::H,              false },
        { KeyCode::I,              false },
        { KeyCode::J,              false },
        { KeyCode::K,              false },
        { KeyCode::L,              false },
        { KeyCode::M,              false },
        { KeyCode::N,              false },
        { KeyCode::O,              false },
        { KeyCode::P,              false },
        { KeyCode::Q,              false },
        { KeyCode::R,              false },
        { KeyCode::S,              false },
        { KeyCode::T,              false },
        { KeyCode::U,              false },
        { KeyCode::V,              false },
        { KeyCode::W,              false },
        { KeyCode::X,              false },
        { KeyCode::Y,              false },
        { KeyCode::Z,              false },
        { KeyCode::Numlock,        false },
        { KeyCode::CapsLock,       false },
        { KeyCode::ScrollLock,     false },
        { KeyCode::RightShift,     false },
        { KeyCode::LeftShift,      false },
        { KeyCode::RightControl,   false },
        { KeyCode::LeftControl,    false },
        { KeyCode::RightAlt,       false },
        { KeyCode::LeftAlt,        false },
        { KeyCode::PrintScreen,    false },
        { KeyCode::Menu,           false }
    };

	dense_map<KeyCode, bool> Input::m_last_mouse_states = {
        { KeyCode::Mouse1,      false},
        { KeyCode::Mouse2,      false},
        { KeyCode::Mouse3,      false}
    };

    void Input::init(GLFWwindow* window)
    {
        m_window = window;
    }

    void Input::update()
    {
		for (auto &[key, state] : m_last_keys_states)
			state = isKeyDown(key);

		for (auto &[button, state] : m_last_mouse_states)
			state = isMouseDown(button);
    }

	bool Input::isKeyDown(KeyCode keyCode)
    {
        return glfwGetKey(m_window, static_cast<int>(keyCode)) == GLFW_PRESS;
    }

	bool Input::wasKeyPressed(KeyCode keyCode)
    {
		return isKeyDown(keyCode) and not m_last_keys_states[keyCode];
    }

	bool Input::wasKeyReleased(KeyCode keyCode)
    {
		return !isKeyDown(keyCode) and m_last_keys_states[keyCode];
    }

	bool Input::isMouseDown(KeyCode keyCode)
    {
        return glfwGetMouseButton(m_window, static_cast<int>(keyCode)) == GLFW_PRESS;
    }

	bool Input::wasMousePressed(KeyCode keyCode)
    {
		return isMouseDown(keyCode) and not m_last_mouse_states[keyCode];
    }

	bool Input::wasMouseReleased(KeyCode keyCode)
    {
		return !isMouseDown(keyCode) and m_last_mouse_states[keyCode];
    }

	glm::uvec2 Input::getMousePosition()
    {
        double x_pos, y_pos;
        glfwGetCursorPos(m_window, &x_pos, &y_pos);

		return glm::uvec2(x_pos, y_pos);
    }

	void Input::setMousePosition(const glm::uvec2 & cursor_position)
	{
		glfwSetCursorPos(m_window, cursor_position.x, cursor_position.y);
	}

    void Input::setMouseCursorVisibility(bool is_visible)
    {
		glfwSetInputMode(m_window, GLFW_CURSOR, is_visible? GLFW_CURSOR_NORMAL: GLFW_CURSOR_DISABLED);
    }

	float Input::getGamepadAxis(int gamepad, int axis)
	{
		assert(gamepad >= GLFW_JOYSTICK_1 and gamepad <= GLFW_JOYSTICK_LAST);

		GLFWgamepadstate state;
		if(glfwGetGamepadState(gamepad, &state))
			return state.axes[axis];

		return 0;
	}

	bool Input::getGamepadButton(int gamepad, int button)
	{
		assert(gamepad >= GLFW_JOYSTICK_1 and gamepad <= GLFW_JOYSTICK_LAST);

		GLFWgamepadstate state;
		if(glfwGetGamepadState(gamepad, &state))
			return state.buttons[button] == GLFW_PRESS;

		return false;
	}

}
