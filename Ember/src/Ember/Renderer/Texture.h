#pragma once

#include "Ember/Renderer/Image.h"

#include <nvrhi/nvrhi.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Ember {

	// Floating-point RGBA image stored as half floats (used for HDR environment maps).
	struct HDRImage
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		std::vector<uint16_t> Pixels; // Width * Height * 4 half floats

		bool IsValid() const { return Width > 0 && Height > 0 && Pixels.size() == size_t(Width) * Height * 4; }
	};

	namespace TextureUtils {

		// Decodes PNG/JPEG/TGA/BMP/... from memory into RGBA8.
		std::optional<Image> DecodeImage(const uint8_t* data, size_t size);
		// Loads a .hdr (Radiance) or any LDR image as linear half-float RGBA.
		std::optional<HDRImage> LoadHDR(const std::filesystem::path& path);

		// Builds the full mip chain of an RGBA8 image on the CPU (gamma-correct averaging for sRGB data).
		std::vector<Image> GenerateMips(const Image& image, bool srgb);

		// Creates a sampled 2D texture with a full mip chain and uploads it through the command list.
		nvrhi::TextureHandle CreateTexture2D(nvrhi::IDevice* device, nvrhi::ICommandList* commandList, const Image& image,
			bool srgb, bool generateMips, const std::string& debugName);
		nvrhi::TextureHandle CreateTextureHDR(nvrhi::IDevice* device, nvrhi::ICommandList* commandList, const HDRImage& image,
			const std::string& debugName);
		// 1x1 texture of a constant color (RGBA8, values 0-255).
		nvrhi::TextureHandle CreateSolidTexture(nvrhi::IDevice* device, nvrhi::ICommandList* commandList,
			uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool srgb, const std::string& debugName);

		uint32_t MipCount(uint32_t width, uint32_t height);

	}

}
