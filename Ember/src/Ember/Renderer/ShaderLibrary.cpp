#include "Ember/Renderer/ShaderLibrary.h"

#include "Ember/Core/Log.h"

#include <EmberShaders.h>

namespace Ember {

	nvrhi::ShaderHandle ShaderLibrary::Get(const std::string& name, nvrhi::ShaderType type)
	{
		const char* suffix = nullptr;
		switch (type)
		{
			case nvrhi::ShaderType::Vertex:  suffix = "_vert"; break;
			case nvrhi::ShaderType::Pixel:   suffix = "_frag"; break;
			case nvrhi::ShaderType::Compute: suffix = "_comp"; break;
			default:
				EMBER_CORE_ERROR("ShaderLibrary: unsupported shader type for '{}'", name);
				return nullptr;
		}

		const std::string key = name + suffix;
		if (auto it = m_Cache.find(key); it != m_Cache.end())
			return it->second;

		const std::span<const uint8_t> blob = EmbeddedShaders::Find(key);
		if (blob.empty())
		{
			EMBER_CORE_ERROR("ShaderLibrary: shader '{}' not found", key);
			return nullptr;
		}

		nvrhi::ShaderDesc desc;
		desc.shaderType = type;
		desc.debugName = key;
		desc.entryName = "main";
		nvrhi::ShaderHandle shader = m_Device->createShader(desc, blob.data(), blob.size());
		if (!shader)
			EMBER_CORE_ERROR("ShaderLibrary: failed to create shader '{}'", key);
		m_Cache[key] = shader;
		return shader;
	}

}
