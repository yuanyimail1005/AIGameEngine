#pragma once

#include "Ember/Renderer/GraphicsDevice.h"

namespace EmberTest {

	// Lazily creates one shared headless graphics device for all GPU tests in the process.
	// Returns nullptr if no Vulkan device is available (GPU tests then fail with a clear message).
	Ember::GraphicsDevice* GetGpu();

	// Destroys the shared device (called from test teardown if needed).
	void ShutdownGpu();

}

// Use at the start of a GPU test case.
#define EMBER_REQUIRE_GPU(var) \
	Ember::GraphicsDevice* var = ::EmberTest::GetGpu(); \
	REQUIRE_MESSAGE(var != nullptr, "No Vulkan 1.3 device available for GPU tests"); \
	var->ResetErrorCount()
