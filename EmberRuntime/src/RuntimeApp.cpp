#include "Ember/Core/Application.h"
#include "Ember/Core/EntryPoint.h"

#include <cstring>

namespace Ember {

	class RuntimeApp : public Application
	{
	public:
		explicit RuntimeApp(const ApplicationSpecification& specification)
			: Application(specification)
		{
		}
	};

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		ApplicationSpecification specification;
		specification.Name = "Ember Runtime";
		specification.CommandLineArgs = args;
		for (int i = 1; i < args.Count; i++)
		{
			if (std::strcmp(args[i], "--headless") == 0)
				specification.Headless = true;
			else if (std::strcmp(args[i], "--frames") == 0 && i + 1 < args.Count)
				specification.MaxFrames = std::strtoull(args[++i], nullptr, 10);
		}
		return new RuntimeApp(specification);
	}

}
