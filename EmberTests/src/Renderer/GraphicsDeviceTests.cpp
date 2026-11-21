#include <doctest/doctest.h>

#include "GpuTestContext.h"

#include "Ember/Renderer/Image.h"

using namespace Ember;

TEST_SUITE("Renderer")
{
	TEST_CASE("GraphicsDevice: headless device is created with an adapter name")
	{
		EMBER_REQUIRE_GPU(gpu);
		CHECK(gpu->IsHeadless());
		CHECK_FALSE(gpu->GetAdapterName().empty());
		CHECK(gpu->GetDevice() != nullptr);
		CHECK(gpu->GetCurrentBackBuffer() == nullptr);
		CHECK(gpu->BeginFrame()); // headless frames always succeed
		gpu->Present();           // no-op
	}

	TEST_CASE("GraphicsDevice: clear offscreen render target and read it back")
	{
		EMBER_REQUIRE_GPU(gpu);
		nvrhi::IDevice* device = gpu->GetDevice();

		for (nvrhi::Format format : { nvrhi::Format::RGBA8_UNORM, nvrhi::Format::RGBA16_FLOAT })
		{
			CAPTURE(static_cast<int>(format));
			nvrhi::TextureDesc desc;
			desc.width = 64;
			desc.height = 32;
			desc.format = format;
			desc.isRenderTarget = true;
			desc.initialState = nvrhi::ResourceStates::RenderTarget;
			desc.keepInitialState = true;
			desc.debugName = "ClearTarget";
			nvrhi::TextureHandle texture = device->createTexture(desc);
			REQUIRE(texture);

			nvrhi::CommandListHandle commandList = device->createCommandList();
			commandList->open();
			commandList->clearTextureFloat(texture, nvrhi::AllSubresources, nvrhi::Color(1.0f, 0.5f, 0.0f, 1.0f));
			commandList->close();
			device->executeCommandList(commandList);

			auto image = ReadbackTexture(device, texture);
			REQUIRE(image.has_value());
			REQUIRE(image->IsValid());
			CHECK(image->Width == 64);
			CHECK(image->Height == 32);
			const uint8_t* pixel = image->GetPixel(10, 20);
			CHECK(pixel[0] == 255);
			CHECK(pixel[1] >= 127);
			CHECK(pixel[1] <= 128);
			CHECK(pixel[2] == 0);
			CHECK(pixel[3] == 255);
		}

		CHECK(gpu->GetErrorCount() == 0);
	}
}
