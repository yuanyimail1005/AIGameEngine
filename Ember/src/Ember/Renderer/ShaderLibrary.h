#pragma once

#include <nvrhi/nvrhi.h>

#include <string>
#include <unordered_map>

namespace Ember {

	// Creates and caches NVRHI shaders from the SPIR-V blobs embedded at build time.
	class ShaderLibrary
	{
	public:
		explicit ShaderLibrary(nvrhi::IDevice* device) : m_Device(device) {}

		// name = shader file base name, e.g. Get("PBR", nvrhi::ShaderType::Pixel) loads PBR.frag.glsl.
		// Returns null (and logs) if the shader does not exist.
		nvrhi::ShaderHandle Get(const std::string& name, nvrhi::ShaderType type);

	private:
		nvrhi::IDevice* m_Device;
		std::unordered_map<std::string, nvrhi::ShaderHandle> m_Cache;
	};

}
