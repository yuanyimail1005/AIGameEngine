#pragma once

#include "Ember/Core/Base.h"
#include "Ember/Core/Events.h"

#include <functional>
#include <string>

struct GLFWwindow;

namespace Ember {

	struct WindowSpecification
	{
		std::string Title = "Ember";
		uint32_t Width = 1600;
		uint32_t Height = 900;
		bool Resizable = true;
		bool Maximized = false;
	};

	// Native OS window (GLFW, no client API - rendering is done through Vulkan).
	class Window
	{
	public:
		using EventCallbackFn = std::function<void(Event&)>;

		explicit Window(const WindowSpecification& specification);
		~Window();

		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;

		// Returns false if GLFW or the native window could not be created.
		bool IsValid() const { return m_Window != nullptr; }

		void PollEvents();

		uint32_t GetWidth() const { return m_Data.Width; }
		uint32_t GetHeight() const { return m_Data.Height; }
		// Framebuffer size in pixels (differs from window size on HiDPI displays).
		void GetFramebufferSize(uint32_t& width, uint32_t& height) const;
		bool IsMinimized() const;

		void SetTitle(const std::string& title);
		const std::string& GetTitle() const { return m_Data.Title; }
		void SetEventCallback(const EventCallbackFn& callback) { m_Data.EventCallback = callback; }
		void SetCursorLocked(bool locked);

		GLFWwindow* GetNativeWindow() const { return m_Window; }

		// GLFW must be initialized before Vulkan instance creation to query required extensions.
		static bool InitializeGLFW();
		static void ShutdownGLFW();

	private:
		struct WindowData
		{
			std::string Title;
			uint32_t Width = 0;
			uint32_t Height = 0;
			EventCallbackFn EventCallback;
		};

		GLFWwindow* m_Window = nullptr;
		WindowData m_Data;
	};

}
