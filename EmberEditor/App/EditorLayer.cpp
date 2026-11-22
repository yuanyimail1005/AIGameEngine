#include "EditorLayer.h"

#include "Ember/Core/Application.h"
#include "Ember/Core/FileSystem.h"
#include "Ember/Core/Input.h"
#include "Ember/Core/Log.h"
#include "Ember/ImGui/ImGuiLayer.h"
#include "Ember/Renderer/GraphicsDevice.h"
#include "Ember/Scripting/ScriptEngine.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <future>
#include <iostream>

namespace Ember {

	EditorLayer::EditorLayer(const EditorOptions& options)
		: Layer("EditorLayer"), m_Options(options)
	{
	}

	EditorLayer::~EditorLayer() = default;

	void EditorLayer::OnAttach()
	{
		Application& app = Application::Get();
		m_Context = CreateScope<EditorContext>(app.GetGraphicsDevice());
		m_Commands = CreateScope<EditorCommands>(*m_Context);
		m_Commands->OnQuit = [&app]() { app.Close(); };

		if (!m_Options.ProjectPath.empty())
		{
			std::string error;
			if (!m_Context->OpenProject(m_Options.ProjectPath, &error))
			{
				EMBER_CORE_ERROR("Cannot open project '{}': {}", m_Options.ProjectPath.string(), error);
				if (m_Options.Headless)
				{
					m_ExitCode = 1;
					app.SetExitCode(1);
					app.Close();
					return;
				}
			}
			else
			{
				m_BrowserDirectory = m_Context->GetProject()->GetAssetDirectory();
			}
		}
		if (!m_Options.ScenePath.empty() && m_Context->HasProject())
		{
			std::string error;
			if (!m_Context->OpenScene(m_Options.ScenePath, &error))
				EMBER_CORE_ERROR("Cannot open scene '{}': {}", m_Options.ScenePath, error);
		}

		if (m_Options.AIServerPort)
		{
			std::string error;
			if (!m_Server.Start(*m_Options.AIServerPort, [this](const std::string& line) { return DispatchFromNetwork(line); }, &error))
			{
				EMBER_CORE_ERROR("AI server: {}", error);
				if (m_Options.Headless)
				{
					m_ExitCode = 1;
					app.SetExitCode(1);
					app.Close();
				}
			}
			else
			{
				// Machine-readable line for tools that launch the editor and wait for it to be ready.
				std::printf("EMBER_AI_SERVER_READY port=%u\n", static_cast<unsigned>(m_Server.GetPort()));
				std::fflush(stdout);
			}
		}
	}

	void EditorLayer::OnDetach()
	{
		m_Server.Stop();
		m_Commands.reset();
		m_Context.reset();
	}

	std::string EditorLayer::DispatchFromNetwork(const std::string& line)
	{
		// Network thread: hand the request to the main thread and wait for the response.
		auto promise = std::make_shared<std::promise<std::string>>();
		std::future<std::string> future = promise->get_future();
		Application::Get().SubmitToMainThread([this, line, promise]()
		{
			promise->set_value(m_Commands ? m_Commands->HandleLine(line) : std::string("{\"id\":null,\"error\":{\"message\":\"editor shutting down\"}}"));
		});
		while (future.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready)
		{
			if (!m_Server.IsRunning())
				return "{\"id\":null,\"error\":{\"message\":\"editor shutting down\"}}";
		}
		return future.get();
	}

	void EditorLayer::RunBatch()
	{
		std::ifstream file(m_Options.BatchFile);
		if (!file)
		{
			EMBER_CORE_ERROR("Cannot open batch file '{}'", m_Options.BatchFile.string());
			m_ExitCode = 1;
			return;
		}
		std::string line;
		int lineNumber = 0;
		int failures = 0;
		while (std::getline(file, line))
		{
			lineNumber++;
			if (line.empty() || line[0] == '#')
				continue;
			const std::string response = m_Commands->HandleLine(line);
			std::cout << response << std::endl;
			if (response.find("\"error\"") != std::string::npos && Json::parse(response).contains("error"))
			{
				EMBER_CORE_ERROR("Batch line {} failed: {}", lineNumber, response);
				failures++;
			}
		}
		m_ExitCode = failures > 0 ? 1 : 0;
	}

	void EditorLayer::RunAutomatedTests()
	{
		GraphicsDevice& device = Application::Get().GetGraphicsDevice();
		device.ResetErrorCount();
		if (!m_Context->HasProject())
		{
			EMBER_CORE_ERROR("--run-tests requires --project");
			m_ExitCode = 1;
			return;
		}

		m_Context->Play();
		Scene& scene = *m_Context->GetActiveScene();
		for (int frame = 0; frame < m_Options.TestFrames; frame++)
		{
			scene.OnUpdateRuntime(Timestep(1.0f / 60.0f));
			Input::EndFrame();
			// Render periodically so rendering of every feature is exercised (and validated) during the run.
			if (frame % 60 == 0)
				m_Context->Capture(320, 180, true);
		}

		int failed = 0;
		int passed = 0;
		ScriptEngine* scripts = scene.GetScriptEngine();
		for (const ScriptTestResult& result : scripts->GetTestResults())
		{
			if (result.Passed)
				passed++;
			else
			{
				failed++;
				std::printf("FAIL  %s: %s\n", result.Name.c_str(), result.Message.c_str());
			}
		}
		for (const std::string& error : scripts->GetErrors())
			std::printf("SCRIPT ERROR  %s\n", error.c_str());
		const std::vector<std::string> missing = scripts->GetMissingExpectedTests();
		// Read everything needed from the runtime scene before Stop() destroys it.
		const uint32_t scriptErrors = scripts->GetErrorCount();
		scripts = nullptr;
		for (const std::string& name : missing)
			std::printf("MISSING  %s (declared with Test.Expect but never passed)\n", name.c_str());

		if (!m_Options.ScreenshotPath.empty())
		{
			std::string error;
			if (!m_Context->CaptureToFile(m_Options.ScreenshotPath, 1280, 720, true, &error))
				std::printf("SCREENSHOT FAILED  %s\n", error.c_str());
		}
		m_Context->Stop();

		const uint32_t gpuErrors = device.GetErrorCount();
		const bool success = failed == 0 && passed > 0 && missing.empty() && scriptErrors == 0 && gpuErrors == 0;
		std::printf("Ember test run: %d passed, %d failed, %u script errors, %u GPU errors -> %s\n", passed, failed,
			scriptErrors, gpuErrors, success ? "SUCCESS" : "FAILURE");
		std::fflush(stdout);
		m_ExitCode = success ? 0 : 1;
	}

	void EditorLayer::OnUpdate(Timestep ts)
	{
		if (!m_Context)
			return;

		if (!m_StartupDone)
		{
			m_StartupDone = true;
			if (!m_Options.BatchFile.empty() || m_Options.RunTests)
			{
				if (!m_Options.BatchFile.empty())
					RunBatch();
				if (m_Options.RunTests && m_ExitCode == 0)
					RunAutomatedTests();
				Application::Get().SetExitCode(m_ExitCode);
				Application::Get().Close();
				return;
			}
		}

		m_Context->OnUpdate(ts, m_ViewportHovered || m_ViewportFocused);
	}

	void EditorLayer::OnRender()
	{
		if (!m_Context || Application::Get().IsHeadless())
			return;
		m_Context->SetViewportSize(static_cast<uint32_t>(std::max(m_ViewportSize.x, 1.0f)), static_cast<uint32_t>(std::max(m_ViewportSize.y, 1.0f)));
		m_Context->RenderViewport();
	}

	void EditorLayer::OnImGuiRender()
	{
		if (!m_Context)
			return;
		DrawDockspace();
		DrawMenuBar();
		DrawToolbar();
		DrawViewport();
		DrawHierarchy();
		DrawInspector();
		DrawAssetBrowser();
		DrawConsole();
		DrawRenderPanel();
		DrawPathPopups();

		if (ImGuiLayer* imgui = Application::Get().GetImGuiLayer())
			imgui->BlockEvents(!m_ViewportHovered);
	}

	void EditorLayer::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& e) { return HandleShortcuts(e); });
		dispatcher.Dispatch<WindowDropEvent>([this](WindowDropEvent& e)
		{
			if (!m_Context || !m_Context->HasProject())
				return false;
			// Dropped files are imported into the asset directory (by extension).
			for (const std::filesystem::path& path : e.GetPaths())
			{
				std::string ext = path.extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				std::string folder = "Imported";
				if (ext == ".gltf" || ext == ".glb") folder = "Models";
				else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga") folder = "Textures";
				else if (ext == ".hdr") folder = "HDRI";
				else if (ext == ".ttf" || ext == ".otf") folder = "Fonts";
				else if (ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".ogg") folder = "Audio";
				else if (ext == ".lua") folder = "Scripts";
				try
				{
					m_Commands->Execute("import_file", { { "source", path.string() }, { "destination", folder + "/" + path.filename().string() } });
					m_StatusMessage = "Imported " + path.filename().string();
				}
				catch (const CommandError& error)
				{
					m_StatusMessage = std::string("Import failed: ") + error.what();
				}
			}
			return true;
		});
	}

}
