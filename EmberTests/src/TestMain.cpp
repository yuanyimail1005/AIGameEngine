#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "Ember/Audio/AudioEngine.h"
#include "Ember/Core/Log.h"

#include "GpuTestContext.h"

#include <cstdlib>

int main(int argc, char** argv)
{
	// Tests never make noise: use the device-less audio engine unless explicitly requested.
	if (!std::getenv("EMBER_TEST_AUDIO_DEVICE"))
	{
#if defined(_WIN32)
		_putenv_s("EMBER_AUDIO", "null");
#else
		setenv("EMBER_AUDIO", "null", 1);
#endif
	}

	Ember::Log::Init();
	// Keep test output readable: only warnings and errors from the engine, unless EMBER_TEST_VERBOSE is set.
	const spdlog::level::level_enum level = std::getenv("EMBER_TEST_VERBOSE") ? spdlog::level::trace : spdlog::level::warn;
	Ember::Log::GetCoreLogger()->set_level(level);
	Ember::Log::GetClientLogger()->set_level(level);

	doctest::Context context;
	context.applyCommandLine(argc, argv);
	const int result = context.run();

	// Destroy GPU objects while logging is still available.
	EmberTest::ShutdownGpu();
	Ember::AudioEngine::Shutdown();

	Ember::Log::Shutdown();
	return result;
}
