#pragma once

#include "Ember/Core/Base.h"
#include "Ember/Core/Events.h"
#include "Ember/Core/Layer.h"
#include "Ember/Core/Timestep.h"
#include "Ember/Core/Window.h"

#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace Ember {

	class GraphicsDevice;
	class ImGuiLayer;

	struct ApplicationCommandLineArgs
	{
		int Count = 0;
		char** Args = nullptr;

		const char* operator[](int index) const { return (index >= 0 && index < Count) ? Args[index] : nullptr; }
	};

	struct ApplicationSpecification
	{
		std::string Name = "Ember Application";
		WindowSpecification Window;
		// Headless: no window or swapchain; layers still update and render offscreen.
		bool Headless = false;
		bool VSync = true;
		// Stop automatically after this many frames (0 = run until closed). Useful for automated tests.
		uint64_t MaxFrames = 0;
		// Fixed timestep used in headless mode (deterministic), in seconds.
		float HeadlessTimestep = 1.0f / 60.0f;
		// Headless mode frame-rate cap (0 = uncapped). Keeps an idle headless editor from spinning a CPU core.
		float HeadlessMaxFPS = 0.0f;
		// Create the Dear ImGui layer (windowed mode only).
		bool EnableImGui = false;
		std::string PreferredGPU;
		std::filesystem::path WorkingDirectory;
		ApplicationCommandLineArgs CommandLineArgs;
	};

	class Application
	{
	public:
		explicit Application(const ApplicationSpecification& specification);
		virtual ~Application();

		Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;

		// False if window or graphics device creation failed. Run() returns immediately in that case.
		bool IsInitialized() const { return m_Initialized; }

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* overlay);

		// Runs the main loop. Returns the process exit code.
		int Run();
		void Close();
		void OnEvent(Event& event);

		// Thread-safe: queue work to run on the main thread at the end of the current frame.
		void SubmitToMainThread(const std::function<void()>& function);

		Window* GetWindow() const { return m_Window.get(); }
		GraphicsDevice& GetGraphicsDevice() const { return *m_GraphicsDevice; }
		const ApplicationSpecification& GetSpecification() const { return m_Specification; }
		uint64_t GetFrameCount() const { return m_FrameCount; }
		float GetLastFrameTime() const { return m_LastFrameDuration; }
		bool IsHeadless() const { return m_Specification.Headless; }
		// Null when ImGui is disabled or running headless.
		ImGuiLayer* GetImGuiLayer() const { return m_ImGuiLayer; }

		static Application& Get() { return *s_Instance; }
		static bool HasInstance() { return s_Instance != nullptr; }

		// Exit code returned by Run() (e.g. automated runs report failures through it).
		void SetExitCode(int code) { m_ExitCode = code; }

	private:
		bool OnWindowClose(WindowCloseEvent& event);
		bool OnWindowResize(WindowResizeEvent& event);
		void ExecuteMainThreadQueue();

	private:
		ApplicationSpecification m_Specification;
		Scope<Window> m_Window;
		Scope<GraphicsDevice> m_GraphicsDevice;
		LayerStack m_LayerStack;
		ImGuiLayer* m_ImGuiLayer = nullptr;

		bool m_Initialized = false;
		bool m_Running = true;
		bool m_Minimized = false;
		int m_ExitCode = 0;
		uint64_t m_FrameCount = 0;
		float m_LastFrameDuration = 0.0f;

		std::vector<std::function<void()>> m_MainThreadQueue;
		std::mutex m_MainThreadQueueMutex;

		static Application* s_Instance;
	};

	// Implemented by the client executable (editor / runtime).
	Application* CreateApplication(ApplicationCommandLineArgs args);

}
