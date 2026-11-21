#pragma once

#include "Ember/Core/Events.h"
#include "Ember/Core/KeyCodes.h"

#include <glm/vec2.hpp>

#include <array>

namespace Ember {

	// Global input state. Driven purely by events (from the window, or injected by tests / the AI control
	// interface), so every input source behaves identically. Call EndFrame() once per frame after the
	// update loop so "just pressed / just released" queries work.
	class Input
	{
	public:
		static bool IsKeyPressed(KeyCode key);
		static bool IsKeyJustPressed(KeyCode key);
		static bool IsKeyJustReleased(KeyCode key);

		static bool IsMouseButtonPressed(MouseButton button);
		static bool IsMouseButtonJustPressed(MouseButton button);
		static bool IsMouseButtonJustReleased(MouseButton button);

		static glm::vec2 GetMousePosition();
		static glm::vec2 GetMouseDelta();
		static glm::vec2 GetScrollDelta();

		// Feeds an input event into the state. Non-input events are ignored.
		static void OnEvent(const Event& event);
		// Advances to the next frame (clears per-frame edges and deltas).
		static void EndFrame();
		// Releases all keys/buttons (e.g. on focus loss or when starting/stopping play mode).
		static void Reset();

	private:
		static size_t KeyIndex(KeyCode key);
		static size_t ButtonIndex(MouseButton button);

		struct State
		{
			std::array<bool, static_cast<size_t>(KeyCode::Count)> Keys{};
			std::array<bool, static_cast<size_t>(KeyCode::Count)> PreviousKeys{};
			std::array<bool, static_cast<size_t>(MouseButton::Count)> Buttons{};
			std::array<bool, static_cast<size_t>(MouseButton::Count)> PreviousButtons{};
			glm::vec2 MousePosition{ 0.0f };
			glm::vec2 PreviousMousePosition{ 0.0f };
			glm::vec2 ScrollDelta{ 0.0f };
			bool HasMousePosition = false;
		};

		static State s_State;
	};

}
