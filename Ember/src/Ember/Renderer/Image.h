#pragma once

#include "Ember/Core/Base.h"

#include <nvrhi/nvrhi.h>

#include <filesystem>
#include <optional>
#include <vector>

namespace Ember {

	// CPU-side 8-bit RGBA image (row-major, top row first).
	struct Image
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		std::vector<uint8_t> Pixels; // Width * Height * 4

		bool IsValid() const { return Width > 0 && Height > 0 && Pixels.size() == size_t(Width) * Height * 4; }

		const uint8_t* GetPixel(uint32_t x, uint32_t y) const { return &Pixels[(size_t(y) * Width + x) * 4]; }

		bool SavePNG(const std::filesystem::path& path) const;
		static std::optional<Image> LoadFromFile(const std::filesystem::path& path);
	};

	// Copies a 2D texture (mip 0, layer 0) back to the CPU. Blocks until the GPU is done.
	// Supported formats: RGBA8_UNORM, SRGBA8_UNORM, BGRA8_UNORM, SBGRA8_UNORM, RGBA16_FLOAT, RGBA32_FLOAT
	// (float formats are clamped to [0,1]). The texture's current state must be tracked by NVRHI.
	std::optional<Image> ReadbackTexture(nvrhi::IDevice* device, nvrhi::ITexture* texture);

}
