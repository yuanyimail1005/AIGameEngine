// Ember runtime: plays an exported game. The project file is found next to the executable (or via --project).

#include "Ember/Core/Application.h"
#include "Ember/Core/EntryPoint.h"
#include "Ember/Core/FileSystem.h"
#include "Ember/Core/Input.h"
#include "Ember/Core/Log.h"
#include "Ember/Project/Project.h"
#include "Ember/Renderer/GraphicsDevice.h"
#include "Ember/Renderer/RenderAssets.h"
#include "Ember/Renderer/SceneRenderer.h"
#include "Ember/Renderer/TextureBlitter.h"
#include "Ember/Scene/Scene.h"
#include "Ember/Scene/SceneSerializer.h"
#include "Ember/Scripting/ScriptEngine.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Ember {

	struct RuntimeOptions
	{
		std::filesystem::path ProjectPath;
		std::filesystem::path ScreenshotPath; // saved when the app exits (headless test runs)
		uint32_t Width = 1280;
		uint32_t Height = 720;
	};

	class RuntimeLayer : public Layer
	{
	public:
		RuntimeLayer(Ref<Project> project, const RuntimeOptions& options)
			: Layer("RuntimeLayer"), m_Project(std::move(project)), m_Options(options)
		{
		}

		void OnAttach() override
		{
			Application& app = Application::Get();
			GraphicsDevice& device = app.GetGraphicsDevice();
			m_Assets = CreateScope<RenderAssets>(device.GetDevice(), m_Project->GetRenderSettings(device.GetAdapterName()));
			m_Assets->SetAssetDirectory(m_Project->GetAssetDirectory());
			m_Renderer = CreateScope<SceneRenderer>(*m_Assets);
			m_Blitter = CreateScope<TextureBlitter>(device.GetDevice(), m_Assets->GetShaders());
			m_CommandList = device.GetDevice()->createCommandList();

			m_Scene = CreateRef<Scene>();
			std::string error;
			const std::string& startScene = m_Project->GetConfig().StartScene;
			if (!SceneSerializer(m_Scene).Deserialize(m_Project->GetAssetPath(startScene), &error))
			{
				EMBER_CORE_CRITICAL("Cannot load start scene '{}': {}", startScene, error);
				app.SetExitCode(1);
				app.Close();
				return;
			}
			m_Scene->SetAssetDirectory(m_Project->GetAssetDirectory());
			Resize();
			m_Scene->OnRuntimeStart();
		}

		void OnDetach() override
		{
			if (m_Scene)
			{
				if (!m_Options.ScreenshotPath.empty())
				{
					if (std::optional<Image> image = m_Renderer->CaptureImage())
						image->SavePNG(m_Options.ScreenshotPath);
				}
				m_Scene->OnRuntimeStop();
			}
			m_Scene.reset();
			m_Blitter.reset();
			m_Renderer.reset();
			m_Assets.reset();
		}

		void OnUpdate(Timestep ts) override
		{
			if (!m_Scene)
				return;
			Resize();
			m_Scene->OnUpdateRuntime(ts);
		}

		void OnRender() override
		{
			if (!m_Scene)
				return;
			std::optional<RenderCamera> camera = SceneRenderer::GetPrimaryCamera(*m_Scene);
			if (!camera)
			{
				if (!m_WarnedNoCamera)
					EMBER_CORE_WARN("The scene has no primary camera; nothing is rendered");
				m_WarnedNoCamera = true;
				return;
			}
			m_Renderer->Render(*m_Scene, *camera);

			GraphicsDevice& device = Application::Get().GetGraphicsDevice();
			if (nvrhi::IFramebuffer* framebuffer = device.GetCurrentFramebuffer())
			{
				m_CommandList->open();
				m_Blitter->Blit(m_CommandList, m_Renderer->GetFinalImage(), framebuffer);
				m_CommandList->close();
				device.GetDevice()->executeCommandList(m_CommandList);
			}
		}

	private:
		void Resize()
		{
			GraphicsDevice& device = Application::Get().GetGraphicsDevice();
			uint32_t width = m_Options.Width, height = m_Options.Height;
			if (!device.IsHeadless() && device.GetBackBufferWidth() > 0)
			{
				width = device.GetBackBufferWidth();
				height = device.GetBackBufferHeight();
			}
			m_Renderer->SetViewportSize(width, height);
			m_Scene->OnViewportResize(width, height);
		}

	private:
		Ref<Project> m_Project;
		RuntimeOptions m_Options;
		Scope<RenderAssets> m_Assets;
		Scope<SceneRenderer> m_Renderer;
		Scope<TextureBlitter> m_Blitter;
		nvrhi::CommandListHandle m_CommandList;
		Ref<Scene> m_Scene;
		bool m_WarnedNoCamera = false;
	};

	class RuntimeApp : public Application
	{
	public:
		RuntimeApp(const ApplicationSpecification& specification, Ref<Project> project, const RuntimeOptions& options)
			: Application(specification)
		{
			if (IsInitialized())
				PushLayer(new RuntimeLayer(std::move(project), options));
		}
	};

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		RuntimeOptions options;
		ApplicationSpecification specification;
		specification.CommandLineArgs = args;
		for (int i = 1; i < args.Count; i++)
		{
			const std::string arg = args[i];
			if (arg == "--headless")
				specification.Headless = true;
			else if (arg == "--frames" && i + 1 < args.Count)
				specification.MaxFrames = std::strtoull(args[++i], nullptr, 10);
			else if (arg == "--project" && i + 1 < args.Count)
				options.ProjectPath = args[++i];
			else if (arg == "--screenshot" && i + 1 < args.Count)
				options.ScreenshotPath = args[++i];
			else if (arg == "--gpu" && i + 1 < args.Count)
				specification.PreferredGPU = args[++i];
			else if (arg == "--help")
			{
				std::printf("Usage: %s [--project <path>] [--headless] [--frames <n>] [--screenshot <file.png>] [--gpu <name>]\n", args[0]);
				std::exit(0);
			}
		}

		if (options.ProjectPath.empty())
			options.ProjectPath = FileSystem::GetExecutableDirectory();
		std::string error;
		Ref<Project> project = Project::Load(options.ProjectPath, &error);
		if (!project)
		{
			EMBER_CORE_CRITICAL("No game project found ({}). Place the .eproject next to the executable or pass --project.", error);
			return nullptr;
		}

		const ProjectConfig& config = project->GetConfig();
		specification.Name = config.Name;
		specification.Window.Title = config.Name;
		specification.Window.Width = config.WindowWidth;
		specification.Window.Height = config.WindowHeight;
		specification.VSync = config.VSync;
		options.Width = config.WindowWidth;
		options.Height = config.WindowHeight;
		return new RuntimeApp(specification, project, options);
	}

}
