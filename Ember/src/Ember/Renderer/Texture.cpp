#include "Ember/Renderer/Texture.h"

#include "Ember/Core/Log.h"

#include <glm/gtc/packing.hpp>
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace Ember::TextureUtils {

	namespace {

		const std::array<float, 256>& SRGBToLinearTable()
		{
			static const std::array<float, 256> s_Table = []()
			{
				std::array<float, 256> table{};
				for (int i = 0; i < 256; i++)
				{
					const float c = static_cast<float>(i) / 255.0f;
					table[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
				}
				return table;
			}();
			return s_Table;
		}

		uint8_t LinearToSRGB8(float linear)
		{
			linear = std::clamp(linear, 0.0f, 1.0f);
			const float c = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
			return static_cast<uint8_t>(std::clamp(c * 255.0f + 0.5f, 0.0f, 255.0f));
		}

	}

	uint32_t MipCount(uint32_t width, uint32_t height)
	{
		uint32_t levels = 1;
		uint32_t size = std::max(width, height);
		while (size > 1)
		{
			size >>= 1;
			levels++;
		}
		return levels;
	}

	std::optional<Image> DecodeImage(const uint8_t* data, size_t size)
	{
		if (!data || size == 0 || size > static_cast<size_t>(INT32_MAX))
			return std::nullopt;
		int width = 0, height = 0, channels = 0;
		stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
		if (!pixels)
			return std::nullopt;
		Image image;
		image.Width = static_cast<uint32_t>(width);
		image.Height = static_cast<uint32_t>(height);
		image.Pixels.assign(pixels, pixels + size_t(width) * size_t(height) * 4);
		stbi_image_free(pixels);
		return image;
	}

	std::optional<HDRImage> LoadHDR(const std::filesystem::path& path)
	{
		int width = 0, height = 0, channels = 0;
		float* pixels = stbi_loadf(path.string().c_str(), &width, &height, &channels, 4);
		if (!pixels)
		{
			EMBER_CORE_ERROR("Failed to load HDR image '{}': {}", path.string(), stbi_failure_reason() ? stbi_failure_reason() : "unknown");
			return std::nullopt;
		}
		HDRImage image;
		image.Width = static_cast<uint32_t>(width);
		image.Height = static_cast<uint32_t>(height);
		const size_t count = size_t(width) * size_t(height) * 4;
		image.Pixels.resize(count);
		for (size_t i = 0; i < count; i++)
		{
			// Clamp to the half-float range; replace NaN/Inf from malformed files.
			float value = pixels[i];
			if (!std::isfinite(value))
				value = 0.0f;
			image.Pixels[i] = glm::packHalf1x16(std::clamp(value, 0.0f, 65000.0f));
		}
		stbi_image_free(pixels);
		return image;
	}

	std::vector<Image> GenerateMips(const Image& image, bool srgb)
	{
		std::vector<Image> mips;
		mips.push_back(image);
		const auto& toLinear = SRGBToLinearTable();

		while (mips.back().Width > 1 || mips.back().Height > 1)
		{
			const Image& source = mips.back();
			Image next;
			next.Width = std::max(source.Width / 2, 1u);
			next.Height = std::max(source.Height / 2, 1u);
			next.Pixels.resize(size_t(next.Width) * next.Height * 4);

			for (uint32_t y = 0; y < next.Height; y++)
			{
				for (uint32_t x = 0; x < next.Width; x++)
				{
					const uint32_t x0 = std::min(x * 2, source.Width - 1);
					const uint32_t x1 = std::min(x * 2 + 1, source.Width - 1);
					const uint32_t y0 = std::min(y * 2, source.Height - 1);
					const uint32_t y1 = std::min(y * 2 + 1, source.Height - 1);
					const uint8_t* p[4] = { source.GetPixel(x0, y0), source.GetPixel(x1, y0), source.GetPixel(x0, y1), source.GetPixel(x1, y1) };
					uint8_t* out = &next.Pixels[(size_t(y) * next.Width + x) * 4];
					for (int c = 0; c < 4; c++)
					{
						if (srgb && c < 3)
						{
							const float sum = toLinear[p[0][c]] + toLinear[p[1][c]] + toLinear[p[2][c]] + toLinear[p[3][c]];
							out[c] = LinearToSRGB8(sum * 0.25f);
						}
						else
						{
							out[c] = static_cast<uint8_t>((p[0][c] + p[1][c] + p[2][c] + p[3][c] + 2) / 4);
						}
					}
				}
			}
			mips.push_back(std::move(next));
		}
		return mips;
	}

	nvrhi::TextureHandle CreateTexture2D(nvrhi::IDevice* device, nvrhi::ICommandList* commandList, const Image& image,
		bool srgb, bool generateMips, const std::string& debugName)
	{
		if (!image.IsValid())
			return nullptr;

		const std::vector<Image> mips = generateMips ? GenerateMips(image, srgb) : std::vector<Image>{ image };

		nvrhi::TextureDesc desc;
		desc.width = image.Width;
		desc.height = image.Height;
		desc.mipLevels = static_cast<uint32_t>(mips.size());
		desc.format = srgb ? nvrhi::Format::SRGBA8_UNORM : nvrhi::Format::RGBA8_UNORM;
		desc.dimension = nvrhi::TextureDimension::Texture2D;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.keepInitialState = true;
		desc.debugName = debugName;
		nvrhi::TextureHandle texture = device->createTexture(desc);
		if (!texture)
			return nullptr;

		for (uint32_t level = 0; level < mips.size(); level++)
			commandList->writeTexture(texture, 0, level, mips[level].Pixels.data(), size_t(mips[level].Width) * 4);
		return texture;
	}

	nvrhi::TextureHandle CreateTextureHDR(nvrhi::IDevice* device, nvrhi::ICommandList* commandList, const HDRImage& image,
		const std::string& debugName)
	{
		if (!image.IsValid())
			return nullptr;
		nvrhi::TextureDesc desc;
		desc.width = image.Width;
		desc.height = image.Height;
		desc.format = nvrhi::Format::RGBA16_FLOAT;
		desc.dimension = nvrhi::TextureDimension::Texture2D;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.keepInitialState = true;
		desc.debugName = debugName;
		nvrhi::TextureHandle texture = device->createTexture(desc);
		if (!texture)
			return nullptr;
		commandList->writeTexture(texture, 0, 0, image.Pixels.data(), size_t(image.Width) * 8);
		return texture;
	}

	nvrhi::TextureHandle CreateSolidTexture(nvrhi::IDevice* device, nvrhi::ICommandList* commandList,
		uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool srgb, const std::string& debugName)
	{
		Image image;
		image.Width = 1;
		image.Height = 1;
		image.Pixels = { r, g, b, a };
		return CreateTexture2D(device, commandList, image, srgb, false, debugName);
	}

}
