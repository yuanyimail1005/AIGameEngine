#include "Ember/Renderer/Image.h"

#include "Ember/Core/Log.h"

#include <glm/gtc/packing.hpp>
#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cstring>

namespace Ember {

	bool Image::SavePNG(const std::filesystem::path& path) const
	{
		if (!IsValid())
			return false;
		std::error_code ec;
		if (path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), ec);
		const int ok = stbi_write_png(path.string().c_str(), static_cast<int>(Width), static_cast<int>(Height), 4,
			Pixels.data(), static_cast<int>(Width * 4));
		if (!ok)
			EMBER_CORE_ERROR("Failed to write PNG '{}'", path.string());
		return ok != 0;
	}

	std::optional<Image> Image::LoadFromFile(const std::filesystem::path& path)
	{
		int width = 0, height = 0, channels = 0;
		stbi_uc* data = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
		if (!data)
			return std::nullopt;
		Image image;
		image.Width = static_cast<uint32_t>(width);
		image.Height = static_cast<uint32_t>(height);
		image.Pixels.assign(data, data + size_t(width) * size_t(height) * 4);
		stbi_image_free(data);
		return image;
	}

	namespace {

		uint8_t FloatToUnorm8(float value)
		{
			return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
		}

	}

	std::optional<Image> ReadbackTexture(nvrhi::IDevice* device, nvrhi::ITexture* texture)
	{
		if (!device || !texture)
			return std::nullopt;

		const nvrhi::TextureDesc& srcDesc = texture->getDesc();
		const nvrhi::Format format = srcDesc.format;
		size_t bytesPerPixel = 0;
		switch (format)
		{
			case nvrhi::Format::RGBA8_UNORM:
			case nvrhi::Format::SRGBA8_UNORM:
			case nvrhi::Format::BGRA8_UNORM:
			case nvrhi::Format::SBGRA8_UNORM:  bytesPerPixel = 4; break;
			case nvrhi::Format::RGBA16_FLOAT:  bytesPerPixel = 8; break;
			case nvrhi::Format::RGBA32_FLOAT:  bytesPerPixel = 16; break;
			default:
				EMBER_CORE_ERROR("ReadbackTexture: unsupported format {}", static_cast<int>(format));
				return std::nullopt;
		}

		nvrhi::TextureDesc stagingDesc;
		stagingDesc.width = srcDesc.width;
		stagingDesc.height = srcDesc.height;
		stagingDesc.format = format;
		stagingDesc.dimension = nvrhi::TextureDimension::Texture2D;
		stagingDesc.debugName = "ReadbackStaging";
		nvrhi::StagingTextureHandle staging = device->createStagingTexture(stagingDesc, nvrhi::CpuAccessMode::Read);
		if (!staging)
		{
			EMBER_CORE_ERROR("ReadbackTexture: failed to create staging texture");
			return std::nullopt;
		}

		nvrhi::CommandListHandle commandList = device->createCommandList();
		commandList->open();
		commandList->copyTexture(staging, nvrhi::TextureSlice(), texture, nvrhi::TextureSlice());
		commandList->close();
		device->executeCommandList(commandList);
		device->waitForIdle();

		size_t rowPitch = 0;
		const auto* mapped = static_cast<const uint8_t*>(
			device->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch));
		if (!mapped)
		{
			EMBER_CORE_ERROR("ReadbackTexture: failed to map staging texture");
			return std::nullopt;
		}

		Image image;
		image.Width = srcDesc.width;
		image.Height = srcDesc.height;
		image.Pixels.resize(size_t(image.Width) * image.Height * 4);

		for (uint32_t y = 0; y < image.Height; y++)
		{
			const uint8_t* row = mapped + rowPitch * y;
			uint8_t* dst = &image.Pixels[size_t(y) * image.Width * 4];
			for (uint32_t x = 0; x < image.Width; x++)
			{
				const uint8_t* src = row + x * bytesPerPixel;
				uint8_t* out = dst + size_t(x) * 4;
				switch (format)
				{
					case nvrhi::Format::RGBA8_UNORM:
					case nvrhi::Format::SRGBA8_UNORM:
						std::memcpy(out, src, 4);
						break;
					case nvrhi::Format::BGRA8_UNORM:
					case nvrhi::Format::SBGRA8_UNORM:
						out[0] = src[2]; out[1] = src[1]; out[2] = src[0]; out[3] = src[3];
						break;
					case nvrhi::Format::RGBA16_FLOAT:
					{
						uint16_t halves[4];
						std::memcpy(halves, src, sizeof(halves));
						for (int c = 0; c < 4; c++)
							out[c] = FloatToUnorm8(glm::unpackHalf1x16(halves[c]));
						break;
					}
					case nvrhi::Format::RGBA32_FLOAT:
					{
						float values[4];
						std::memcpy(values, src, sizeof(values));
						for (int c = 0; c < 4; c++)
							out[c] = FloatToUnorm8(values[c]);
						break;
					}
					default:
						break;
				}
			}
		}

		device->unmapStagingTexture(staging);
		return image;
	}

}
