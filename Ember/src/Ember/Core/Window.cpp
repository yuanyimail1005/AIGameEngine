#include "Ember/Core/Window.h"

#include "Ember/Core/Log.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <vector>

namespace Ember {

	namespace {

		bool s_GLFWInitialized = false;
		uint32_t s_WindowCount = 0;

		void GLFWErrorCallback(int error, const char* description)
		{
			EMBER_CORE_ERROR("GLFW error ({}): {}", error, description);
		}

	}

	bool Window::InitializeGLFW()
	{
		if (s_GLFWInitialized)
			return true;

		glfwSetErrorCallback(GLFWErrorCallback);
		if (!glfwInit())
		{
			EMBER_CORE_ERROR("Failed to initialize GLFW");
			return false;
		}
		s_GLFWInitialized = true;
		return true;
	}

	void Window::ShutdownGLFW()
	{
		if (s_GLFWInitialized && s_WindowCount == 0)
		{
			glfwTerminate();
			s_GLFWInitialized = false;
		}
	}

	Window::Window(const WindowSpecification& specification)
	{
		m_Data.Title = specification.Title;
		m_Data.Width = specification.Width;
		m_Data.Height = specification.Height;

		if (!InitializeGLFW())
			return;

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, specification.Resizable ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_MAXIMIZED, specification.Maximized ? GLFW_TRUE : GLFW_FALSE);

		m_Window = glfwCreateWindow(static_cast<int>(specification.Width), static_cast<int>(specification.Height),
			specification.Title.c_str(), nullptr, nullptr);
		if (!m_Window)
		{
			EMBER_CORE_ERROR("Failed to create window '{}'", specification.Title);
			return;
		}
		s_WindowCount++;

		int width = 0, height = 0;
		glfwGetWindowSize(m_Window, &width, &height);
		m_Data.Width = static_cast<uint32_t>(width);
		m_Data.Height = static_cast<uint32_t>(height);

		glfwSetWindowUserPointer(m_Window, &m_Data);

		glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int w, int h)
		{
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			data.Width = static_cast<uint32_t>(w);
			data.Height = static_cast<uint32_t>(h);
			if (data.EventCallback)
			{
				WindowResizeEvent event(data.Width, data.Height);
				data.EventCallback(event);
			}
		});

		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
		{
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			if (data.EventCallback)
			{
				WindowCloseEvent event;
				data.EventCallback(event);
			}
		});

		glfwSetWindowFocusCallback(m_Window, [](GLFWwindow* window, int focused)
		{
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			if (data.EventCallback)
			{
				WindowFocusEvent event(focused == GLFW_TRUE);
				data.EventCallback(event);
			}
		});

		glfwSetDropCallback(m_Window, [](GLFWwindow* window, int count, const char** paths)
		{
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			if (!data.EventCallback)
				return;
			std::vector<std::filesystem::path> dropped;
			dropped.reserve(static_cast<size_t>(count));
			for (int i = 0; i < count; i++)
				dropped.emplace_back(paths[i]);
			WindowDropEvent event(std::move(dropped));
			data.EventCallback(event);
		});

		glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
		{
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			if (!data.EventCallback || key < 0 || key >= static_cast<int>(KeyCode::Count))
				return;
			const KeyCode code = static_cast<KeyCode>(key);
			switch (action)
			{
				case GLFW_PRESS:
				{
					KeyPressedEvent event(code, false);
					data.EventCallback(event);
					break;
				}
				case GLFW_REPEAT:
				{
					KeyPressedEvent event(code, true);
					data.EventCallback(event);
					break;
				}
				case GLFW_RELEASE:
				{
					KeyReleasedEvent event(code);
					data.EventCallback(event);
					break;
				}
				default:
					break;
			}
		});

		glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int codepoint)
		{
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			if (data.EventCallback)
			{
				KeyTypedEvent event(codepoint);
				data.EventCallback(event);
			}
		});

		glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int /*mods*/)
		{
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			if (!data.EventCallback || button < 0 || button >= static_cast<int>(MouseButton::Count))
				return;
			const MouseButton code = static_cast<MouseButton>(button);
			if (action == GLFW_PRESS)
			{
				MouseButtonPressedEvent event(code);
				data.EventCallback(event);
			}
			else if (action == GLFW_RELEASE)
			{
				MouseButtonReleasedEvent event(code);
				data.EventCallback(event);
			}
		});

		glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xOffset, double yOffset)
		{
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			if (data.EventCallback)
			{
				MouseScrolledEvent event(static_cast<float>(xOffset), static_cast<float>(yOffset));
				data.EventCallback(event);
			}
		});

		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double x, double y)
		{
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			if (data.EventCallback)
			{
				MouseMovedEvent event(static_cast<float>(x), static_cast<float>(y));
				data.EventCallback(event);
			}
		});
	}

	Window::~Window()
	{
		if (m_Window)
		{
			glfwDestroyWindow(m_Window);
			m_Window = nullptr;
			s_WindowCount--;
		}
	}

	void Window::PollEvents()
	{
		glfwPollEvents();
	}

	void Window::GetFramebufferSize(uint32_t& width, uint32_t& height) const
	{
		int w = 0, h = 0;
		if (m_Window)
			glfwGetFramebufferSize(m_Window, &w, &h);
		width = static_cast<uint32_t>(w);
		height = static_cast<uint32_t>(h);
	}

	bool Window::IsMinimized() const
	{
		return m_Window && glfwGetWindowAttrib(m_Window, GLFW_ICONIFIED) == GLFW_TRUE;
	}

	void Window::SetTitle(const std::string& title)
	{
		m_Data.Title = title;
		if (m_Window)
			glfwSetWindowTitle(m_Window, title.c_str());
	}

	void Window::SetCursorLocked(bool locked)
	{
		if (m_Window)
			glfwSetInputMode(m_Window, GLFW_CURSOR, locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
	}

}
