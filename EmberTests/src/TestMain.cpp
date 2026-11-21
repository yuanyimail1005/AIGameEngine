#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "Ember/Core/Log.h"

#include "GpuTestContext.h"

#include <cstdlib>

int main(int argc, char** argv)
{
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

	Ember::Log::Shutdown();
	return result;
}
