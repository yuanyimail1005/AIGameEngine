#pragma once

#include <cstdint>
#include <string_view>

namespace Ember {

	// Values intentionally match GLFW key codes.
	enum class KeyCode : uint16_t
	{
		Unknown = 0,
		Space = 32, Apostrophe = 39, Comma = 44, Minus = 45, Period = 46, Slash = 47,
		D0 = 48, D1, D2, D3, D4, D5, D6, D7, D8, D9,
		Semicolon = 59, Equal = 61,
		A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
		LeftBracket = 91, Backslash = 92, RightBracket = 93, GraveAccent = 96,
		World1 = 161, World2 = 162,
		Escape = 256, Enter, Tab, Backspace, Insert, Delete, Right, Left, Down, Up, PageUp, PageDown, Home, End,
		CapsLock = 280, ScrollLock, NumLock, PrintScreen, Pause,
		F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20,
		F21, F22, F23, F24, F25,
		KP0 = 320, KP1, KP2, KP3, KP4, KP5, KP6, KP7, KP8, KP9,
		KPDecimal = 330, KPDivide, KPMultiply, KPSubtract, KPAdd, KPEnter, KPEqual,
		LeftShift = 340, LeftControl, LeftAlt, LeftSuper, RightShift, RightControl, RightAlt, RightSuper, Menu,

		Count = 349
	};

	// Values intentionally match GLFW mouse button codes.
	enum class MouseButton : uint8_t
	{
		Left = 0, Right = 1, Middle = 2, Button3 = 3, Button4 = 4, Button5 = 5, Button6 = 6, Button7 = 7,
		Count = 8
	};

	// Name <-> code conversion, used by the scripting API and AI control interface ("W", "Space", "Left").
	std::string_view KeyCodeToString(KeyCode key);
	KeyCode KeyCodeFromString(std::string_view name);
	std::string_view MouseButtonToString(MouseButton button);
	MouseButton MouseButtonFromString(std::string_view name, bool* outValid = nullptr);

}
