#include "Ember/Core/Input.h"

namespace Ember {

	Input::State Input::s_State;

	size_t Input::KeyIndex(KeyCode key)
	{
		const size_t index = static_cast<size_t>(key);
		return index < s_State.Keys.size() ? index : 0;
	}

	size_t Input::ButtonIndex(MouseButton button)
	{
		const size_t index = static_cast<size_t>(button);
		return index < s_State.Buttons.size() ? index : 0;
	}

	bool Input::IsKeyPressed(KeyCode key)
	{
		return key != KeyCode::Unknown && s_State.Keys[KeyIndex(key)];
	}

	bool Input::IsKeyJustPressed(KeyCode key)
	{
		const size_t i = KeyIndex(key);
		return key != KeyCode::Unknown && s_State.Keys[i] && !s_State.PreviousKeys[i];
	}

	bool Input::IsKeyJustReleased(KeyCode key)
	{
		const size_t i = KeyIndex(key);
		return key != KeyCode::Unknown && !s_State.Keys[i] && s_State.PreviousKeys[i];
	}

	bool Input::IsMouseButtonPressed(MouseButton button)
	{
		return s_State.Buttons[ButtonIndex(button)];
	}

	bool Input::IsMouseButtonJustPressed(MouseButton button)
	{
		const size_t i = ButtonIndex(button);
		return s_State.Buttons[i] && !s_State.PreviousButtons[i];
	}

	bool Input::IsMouseButtonJustReleased(MouseButton button)
	{
		const size_t i = ButtonIndex(button);
		return !s_State.Buttons[i] && s_State.PreviousButtons[i];
	}

	glm::vec2 Input::GetMousePosition()
	{
		return s_State.MousePosition;
	}

	glm::vec2 Input::GetMouseDelta()
	{
		return s_State.MousePosition - s_State.PreviousMousePosition;
	}

	glm::vec2 Input::GetScrollDelta()
	{
		return s_State.ScrollDelta;
	}

	void Input::OnEvent(const Event& event)
	{
		switch (event.GetEventType())
		{
			case EventType::KeyPressed:
			{
				const KeyCode key = static_cast<const KeyPressedEvent&>(event).GetKeyCode();
				if (key != KeyCode::Unknown)
					s_State.Keys[KeyIndex(key)] = true;
				break;
			}
			case EventType::KeyReleased:
			{
				const KeyCode key = static_cast<const KeyReleasedEvent&>(event).GetKeyCode();
				if (key != KeyCode::Unknown)
					s_State.Keys[KeyIndex(key)] = false;
				break;
			}
			case EventType::MouseButtonPressed:
				s_State.Buttons[ButtonIndex(static_cast<const MouseButtonPressedEvent&>(event).GetMouseButton())] = true;
				break;
			case EventType::MouseButtonReleased:
				s_State.Buttons[ButtonIndex(static_cast<const MouseButtonReleasedEvent&>(event).GetMouseButton())] = false;
				break;
			case EventType::MouseMoved:
			{
				const auto& moved = static_cast<const MouseMovedEvent&>(event);
				s_State.MousePosition = { moved.GetX(), moved.GetY() };
				if (!s_State.HasMousePosition)
				{
					// Avoid a huge spurious delta on the very first mouse event.
					s_State.PreviousMousePosition = s_State.MousePosition;
					s_State.HasMousePosition = true;
				}
				break;
			}
			case EventType::MouseScrolled:
			{
				const auto& scrolled = static_cast<const MouseScrolledEvent&>(event);
				s_State.ScrollDelta += glm::vec2(scrolled.GetXOffset(), scrolled.GetYOffset());
				break;
			}
			case EventType::WindowFocus:
				if (!static_cast<const WindowFocusEvent&>(event).IsFocused())
				{
					s_State.Keys.fill(false);
					s_State.Buttons.fill(false);
				}
				break;
			default:
				break;
		}
	}

	void Input::EndFrame()
	{
		s_State.PreviousKeys = s_State.Keys;
		s_State.PreviousButtons = s_State.Buttons;
		s_State.PreviousMousePosition = s_State.MousePosition;
		s_State.ScrollDelta = glm::vec2(0.0f);
	}

	void Input::Reset()
	{
		const glm::vec2 mouse = s_State.MousePosition;
		const bool hasMouse = s_State.HasMousePosition;
		s_State = State{};
		s_State.MousePosition = mouse;
		s_State.PreviousMousePosition = mouse;
		s_State.HasMousePosition = hasMouse;
	}

}
