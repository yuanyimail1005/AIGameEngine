#pragma once

// Include exactly once, in the client executable's main translation unit.

#include "Ember/Core/Application.h"
#include "Ember/Core/Log.h"

int main(int argc, char** argv)
{
	::Ember::Log::Init();

	int exitCode = 1;
	{
		::Ember::Application* app = ::Ember::CreateApplication({ argc, argv });
		if (app)
		{
			exitCode = app->Run();
			delete app;
		}
	}

	::Ember::Log::Shutdown();
	return exitCode;
}
