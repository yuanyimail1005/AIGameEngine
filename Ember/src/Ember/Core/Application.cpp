#include "Ember/Core/Application.h"

#include "Ember/Core/Assert.h"
#include "Ember/Core/Input.h"
#include "Ember/Core/Log.h"
#include "Ember/ImGui/ImGuiLayer.h"
#include "Ember/Renderer/GraphicsDevice.h"

#include <chrono>
#include <system_error>
#include <thread>

namespace Ember {

	Application* Application::s_Instance = nullptr;

	Application::Application(const ApplicationSpecification& specification)
		: m_Specification(specification)
	{
		EMBER_CORE_VERIFY(!s_Instance, "Only one Application may exist at a time");
		s_Instance = this;

		if (!m_Specification.WorkingDirectory.empty())
		{
			std::error_code ec;
			std::filesystem::current_path(m_Specification.WorkingDirectory, ec);
			if (ec)
				EMBER_CORE_ERROR("Failed to set working directory to '{}': {}", m_Specification.WorkingDirectory.string(), ec.message());
		}

		GraphicsDeviceSpecification deviceSpec;
		deviceSpec.VSync = m_Specification.VSync;
		deviceSpec.PreferredAdapter = m_Specification.PreferredGPU;

		if (!m_Specification.Headless)
		{
			WindowSpecification windowSpec = m_Specification.Window;
			if (windowSpec.Title.empty())
				windowSpec.Title = m_Specification.Name;
			m_Window = CreateScope<Window>(windowSpec);
			if (!m_Window->IsValid())
			{
				EMBER_CORE_ERROR("Window creation failed - is a display available? (use --headless otherwise)");
				m_Window.reset();
				return;
			}
			m_Window->SetEventCallback(EMBER_BIND_EVENT_FN(OnEvent));
			deviceSpec.TargetWindow = m_Window.get();
		}

		m_GraphicsDevice = GraphicsDevice::Create(deviceSpec);
		if (!m_GraphicsDevice)
		{
			EMBER_CORE_ERROR("Graphics device creation failed");
			return;
		}

		m_Initialized = true;

		if (m_Specification.EnableImGui && !m_Specification.Headless)
		{
			m_ImGuiLayer = new ImGuiLayer();
			PushOverlay(m_ImGuiLayer);
		}
	}

	Application::~Application()
	{
		// Layers own GPU resources: destroy them before the device.
		m_LayerStack.Clear();
		m_ImGuiLayer = nullptr;
		if (m_GraphicsDevice)
			m_GraphicsDevice->GetDevice()->waitForIdle();
		m_GraphicsDevice.reset();
		m_Window.reset();
		Window::ShutdownGLFW();
		s_Instance = nullptr;
	}

	void Application::PushLayer(Layer* layer)
	{
		m_LayerStack.PushLayer(layer);
	}

	void Application::PushOverlay(Layer* overlay)
	{
		m_LayerStack.PushOverlay(overlay);
	}

	void Application::Close()
	{
		m_Running = false;
	}

	void Application::SubmitToMainThread(const std::function<void()>& function)
	{
		std::scoped_lock lock(m_MainThreadQueueMutex);
		m_MainThreadQueue.emplace_back(function);
	}

	void Application::ExecuteMainThreadQueue()
	{
		std::vector<std::function<void()>> queue;
		{
			std::scoped_lock lock(m_MainThreadQueueMutex);
			queue.swap(m_MainThreadQueue);
		}
		for (auto& function : queue)
			function();
	}

	void Application::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<WindowCloseEvent>(EMBER_BIND_EVENT_FN(OnWindowClose));
		dispatcher.Dispatch<WindowResizeEvent>(EMBER_BIND_EVENT_FN(OnWindowResize));

		for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
		{
			if (event.Handled)
				break;
			(*it)->OnEvent(event);
		}

		// Input state tracks raw device state regardless of which layer consumed the event,
		// but focus loss / key releases must always reach it.
		Input::OnEvent(event);
	}

	int Application::Run()
	{
		if (!m_Initialized)
			return 1;

		Timer frameTimer;
		while (m_Running)
		{
			const float elapsed = frameTimer.Elapsed();
			frameTimer.Reset();
			const Timestep timestep = m_Specification.Headless ? Timestep(m_Specification.HeadlessTimestep) : Timestep(elapsed);
			m_LastFrameDuration = elapsed;

			if (m_Window)
				m_Window->PollEvents();

			ExecuteMainThreadQueue();

			if (!m_Minimized)
			{
				for (Layer* layer : m_LayerStack)
					layer->OnUpdate(timestep);

				if (m_GraphicsDevice->BeginFrame())
				{
					for (Layer* layer : m_LayerStack)
						layer->OnRender();
					if (m_ImGuiLayer)
					{
						m_ImGuiLayer->Begin();
						for (Layer* layer : m_LayerStack)
							layer->OnImGuiRender();
						m_ImGuiLayer->End();
					}
					m_GraphicsDevice->Present();
				}
			}

			Input::EndFrame();

			if (m_GraphicsDevice->GetErrorCount() > 0 && m_Specification.Headless && m_Specification.MaxFrames > 0)
			{
				EMBER_CORE_ERROR("Graphics errors reported during automated run; stopping");
				m_ExitCode = 2;
				m_Running = false;
			}

			m_FrameCount++;
			if (m_Specification.MaxFrames > 0 && m_FrameCount >= m_Specification.MaxFrames)
				m_Running = false;

			if (m_Specification.Headless && m_Specification.HeadlessMaxFPS > 0.0f)
			{
				const float frameBudget = 1.0f / m_Specification.HeadlessMaxFPS;
				const float spent = frameTimer.Elapsed();
				if (spent < frameBudget)
					std::this_thread::sleep_for(std::chrono::duration<float>(frameBudget - spent));
			}
		}

		m_GraphicsDevice->GetDevice()->waitForIdle();
		return m_ExitCode;
	}

	bool Application::OnWindowClose(WindowCloseEvent& /*event*/)
	{
		m_Running = false;
		return true;
	}

	bool Application::OnWindowResize(WindowResizeEvent& event)
	{
		m_Minimized = event.GetWidth() == 0 || event.GetHeight() == 0;
		if (!m_Minimized)
			m_GraphicsDevice->OnWindowResize();
		return false;
	}

}
