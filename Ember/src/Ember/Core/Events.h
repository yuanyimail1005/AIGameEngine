#pragma once

#include "Ember/Core/Base.h"
#include "Ember/Core/KeyCodes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Ember {

	enum class EventType : uint8_t
	{
		None = 0,
		WindowClose, WindowResize, WindowFocus, WindowDrop,
		KeyPressed, KeyReleased, KeyTyped,
		MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled
	};

	enum EventCategory : uint32_t
	{
		EventCategoryNone        = 0,
		EventCategoryApplication = EMBER_BIT(0),
		EventCategoryInput       = EMBER_BIT(1),
		EventCategoryKeyboard    = EMBER_BIT(2),
		EventCategoryMouse       = EMBER_BIT(3),
	};

#define EMBER_EVENT_CLASS_TYPE(type) \
	static EventType GetStaticType() { return EventType::type; } \
	EventType GetEventType() const override { return GetStaticType(); } \
	const char* GetName() const override { return #type; }

#define EMBER_EVENT_CLASS_CATEGORY(category) \
	uint32_t GetCategoryFlags() const override { return category; }

	class Event
	{
	public:
		virtual ~Event() = default;

		virtual EventType GetEventType() const = 0;
		virtual const char* GetName() const = 0;
		virtual uint32_t GetCategoryFlags() const = 0;

		bool IsInCategory(EventCategory category) const { return (GetCategoryFlags() & category) != 0; }

		bool Handled = false;
	};

	class EventDispatcher
	{
	public:
		explicit EventDispatcher(Event& event) : m_Event(event) {}

		// Calls func(T&) if the event is of type T. func returns true if it consumed the event.
		template<typename T, typename F>
		bool Dispatch(const F& func)
		{
			if (m_Event.GetEventType() == T::GetStaticType())
			{
				m_Event.Handled |= func(static_cast<T&>(m_Event));
				return true;
			}
			return false;
		}

	private:
		Event& m_Event;
	};

	// ---------------------------------------------------------------------------------------------
	// Application events

	class WindowCloseEvent : public Event
	{
	public:
		EMBER_EVENT_CLASS_TYPE(WindowClose)
		EMBER_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class WindowResizeEvent : public Event
	{
	public:
		WindowResizeEvent(uint32_t width, uint32_t height) : m_Width(width), m_Height(height) {}

		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }

		EMBER_EVENT_CLASS_TYPE(WindowResize)
		EMBER_EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		uint32_t m_Width, m_Height;
	};

	class WindowFocusEvent : public Event
	{
	public:
		explicit WindowFocusEvent(bool focused) : m_Focused(focused) {}
		bool IsFocused() const { return m_Focused; }

		EMBER_EVENT_CLASS_TYPE(WindowFocus)
		EMBER_EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		bool m_Focused;
	};

	class WindowDropEvent : public Event
	{
	public:
		explicit WindowDropEvent(std::vector<std::filesystem::path> paths) : m_Paths(std::move(paths)) {}
		const std::vector<std::filesystem::path>& GetPaths() const { return m_Paths; }

		EMBER_EVENT_CLASS_TYPE(WindowDrop)
		EMBER_EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		std::vector<std::filesystem::path> m_Paths;
	};

	// ---------------------------------------------------------------------------------------------
	// Keyboard events

	class KeyPressedEvent : public Event
	{
	public:
		KeyPressedEvent(KeyCode key, bool isRepeat) : m_Key(key), m_IsRepeat(isRepeat) {}

		KeyCode GetKeyCode() const { return m_Key; }
		bool IsRepeat() const { return m_IsRepeat; }

		EMBER_EVENT_CLASS_TYPE(KeyPressed)
		EMBER_EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

	private:
		KeyCode m_Key;
		bool m_IsRepeat;
	};

	class KeyReleasedEvent : public Event
	{
	public:
		explicit KeyReleasedEvent(KeyCode key) : m_Key(key) {}
		KeyCode GetKeyCode() const { return m_Key; }

		EMBER_EVENT_CLASS_TYPE(KeyReleased)
		EMBER_EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

	private:
		KeyCode m_Key;
	};

	class KeyTypedEvent : public Event
	{
	public:
		explicit KeyTypedEvent(uint32_t codepoint) : m_Codepoint(codepoint) {}
		uint32_t GetCodepoint() const { return m_Codepoint; }

		EMBER_EVENT_CLASS_TYPE(KeyTyped)
		EMBER_EVENT_CLASS_CATEGORY(EventCategoryKeyboard | EventCategoryInput)

	private:
		uint32_t m_Codepoint;
	};

	// ---------------------------------------------------------------------------------------------
	// Mouse events

	class MouseMovedEvent : public Event
	{
	public:
		MouseMovedEvent(float x, float y) : m_X(x), m_Y(y) {}
		float GetX() const { return m_X; }
		float GetY() const { return m_Y; }

		EMBER_EVENT_CLASS_TYPE(MouseMoved)
		EMBER_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)

	private:
		float m_X, m_Y;
	};

	class MouseScrolledEvent : public Event
	{
	public:
		MouseScrolledEvent(float xOffset, float yOffset) : m_XOffset(xOffset), m_YOffset(yOffset) {}
		float GetXOffset() const { return m_XOffset; }
		float GetYOffset() const { return m_YOffset; }

		EMBER_EVENT_CLASS_TYPE(MouseScrolled)
		EMBER_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)

	private:
		float m_XOffset, m_YOffset;
	};

	class MouseButtonPressedEvent : public Event
	{
	public:
		explicit MouseButtonPressedEvent(MouseButton button) : m_Button(button) {}
		MouseButton GetMouseButton() const { return m_Button; }

		EMBER_EVENT_CLASS_TYPE(MouseButtonPressed)
		EMBER_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)

	private:
		MouseButton m_Button;
	};

	class MouseButtonReleasedEvent : public Event
	{
	public:
		explicit MouseButtonReleasedEvent(MouseButton button) : m_Button(button) {}
		MouseButton GetMouseButton() const { return m_Button; }

		EMBER_EVENT_CLASS_TYPE(MouseButtonReleased)
		EMBER_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)

	private:
		MouseButton m_Button;
	};

}
