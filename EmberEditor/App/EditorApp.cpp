#include "EditorLayer.h"

#include "Ember/Core/Application.h"
#include "Ember/Core/EntryPoint.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace Ember {

	namespace {

		void PrintUsage()
		{
			std::printf(
				"Usage: EmberEditor [options]\n"
				"  --project <path>       open a project (.eproject file or directory)\n"
				"  --scene <asset path>   open a scene after the project\n"
				"  --headless             run without a window (offscreen rendering; for AI agents, CI)\n"
				"  --ai-server [port]     start the AI control server on 127.0.0.1 (default port 7820, 0 = any)\n"
				"  --batch <file.jsonl>   execute JSON commands (one per line), print responses, exit\n"
				"  --run-tests            play the scene, require all script Test checks to pass, exit\n"
				"  --frames <n>           frames to simulate with --run-tests (default 600)\n"
				"  --screenshot <file>    with --run-tests: save the final frame as PNG\n"
				"  --gpu <name>           prefer a GPU whose name contains <name> (e.g. llvmpipe)\n"
				"  --help                 show this help\n");
		}

	}

	class EditorApplication final : public Application
	{
	public:
		EditorApplication(const ApplicationSpecification& specification, const EditorOptions& options)
			: Application(specification)
		{
			if (!IsInitialized())
				return;
			m_Editor = new EditorLayer(options);
			PushLayer(m_Editor);
		}

		~EditorApplication() override = default;

	private:
		EditorLayer* m_Editor = nullptr;
	};

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		ApplicationSpecification specification;
		specification.Name = "Ember Editor";
		specification.Window.Title = "Ember Editor";
		specification.Window.Width = 1600;
		specification.Window.Height = 900;
		specification.Window.Maximized = true;
		specification.EnableImGui = true;
		specification.HeadlessMaxFPS = 60.0f;
		specification.CommandLineArgs = args;

		EditorOptions options;
		for (int i = 1; i < args.Count; i++)
		{
			const std::string arg = args[i];
			auto next = [&]() -> std::string
			{
				if (i + 1 >= args.Count)
				{
					std::fprintf(stderr, "Missing value for %s\n", arg.c_str());
					std::exit(2);
				}
				return args[++i];
			};
			if (arg == "--project")
				options.ProjectPath = next();
			else if (arg == "--scene")
				options.ScenePath = next();
			else if (arg == "--headless")
				options.Headless = true;
			else if (arg == "--ai-server")
			{
				uint16_t port = 7820;
				if (i + 1 < args.Count && args[i + 1][0] != '-')
					port = static_cast<uint16_t>(std::strtoul(args[++i], nullptr, 10));
				options.AIServerPort = port;
			}
			else if (arg == "--batch")
				options.BatchFile = next();
			else if (arg == "--run-tests")
				options.RunTests = true;
			else if (arg == "--frames")
				options.TestFrames = std::max(1, std::atoi(next().c_str()));
			else if (arg == "--screenshot")
				options.ScreenshotPath = next();
			else if (arg == "--gpu")
				specification.PreferredGPU = next();
			else if (arg == "--help" || arg == "-h")
			{
				PrintUsage();
				std::exit(0);
			}
			else
			{
				std::fprintf(stderr, "Unknown option '%s'\n", arg.c_str());
				PrintUsage();
				std::exit(2);
			}
		}

		// Batch and test runs never need a window.
		if (!options.BatchFile.empty() || options.RunTests)
			options.Headless = true;
		specification.Headless = options.Headless;
		return new EditorApplication(specification, options);
	}

}
