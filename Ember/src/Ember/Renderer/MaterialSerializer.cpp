#include "Ember/Renderer/MaterialSerializer.h"

#include "Ember/Core/FileSystem.h"
#include "Ember/Core/Log.h"
#include "Ember/Core/YAMLUtils.h"

#include <algorithm>
#include <cctype>

namespace Ember::MaterialSerializer {

	namespace {

		template<typename T>
		bool Read(const YAML::Node& node, const char* key, T& value)
		{
			if (!node[key])
				return true;
			return ReadYAML(node, key, value);
		}

		bool ReadAlphaMode(const YAML::Node& node, AlphaMode& mode)
		{
			const YAML::Node child = node["AlphaMode"];
			if (!child)
				return true;
			if (!child.IsScalar())
				return false;
			std::string text = child.Scalar();
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (text == "opaque")
				mode = AlphaMode::Opaque;
			else if (text == "mask")
				mode = AlphaMode::Mask;
			else if (text == "blend")
				mode = AlphaMode::Blend;
			else
				return false;
			return true;
		}

	}

	const char* AlphaModeToString(AlphaMode mode)
	{
		switch (mode)
		{
			case AlphaMode::Opaque: return "Opaque";
			case AlphaMode::Mask:   return "Mask";
			case AlphaMode::Blend:  return "Blend";
		}
		return "Opaque";
	}

	YAML::Node ToYAML(const MaterialParameters& m)
	{
		YAML::Node n;
		n["BaseColor"] = m.BaseColor;
		n["Metallic"] = m.Metallic;
		n["Roughness"] = m.Roughness;
		n["EmissiveColor"] = m.EmissiveColor;
		n["EmissiveStrength"] = m.EmissiveStrength;
		n["NormalScale"] = m.NormalScale;
		n["OcclusionStrength"] = m.OcclusionStrength;
		n["AlphaMode"] = AlphaModeToString(m.Alpha);
		n["AlphaCutoff"] = m.AlphaCutoff;
		n["DoubleSided"] = m.DoubleSided;
		n["UVTiling"] = m.UVTiling;
		n["BaseColorTexture"] = m.BaseColorTexture;
		n["MetallicRoughnessTexture"] = m.MetallicRoughnessTexture;
		n["NormalTexture"] = m.NormalTexture;
		n["OcclusionTexture"] = m.OcclusionTexture;
		n["EmissiveTexture"] = m.EmissiveTexture;
		return n;
	}

	bool FromYAML(const YAML::Node& n, MaterialParameters& m)
	{
		if (!n || !n.IsMap())
			return false;
		bool ok = true;
		if (const YAML::Node color = n["BaseColor"]; color && color.IsSequence() && color.size() == 3)
		{
			glm::vec3 rgb;
			if (ReadYAML(n, "BaseColor", rgb))
				m.BaseColor = glm::vec4(rgb, m.BaseColor.a);
			else
				ok = false;
		}
		else
		{
			ok &= Read(n, "BaseColor", m.BaseColor);
		}
		ok &= Read(n, "Metallic", m.Metallic);
		ok &= Read(n, "Roughness", m.Roughness);
		ok &= Read(n, "EmissiveColor", m.EmissiveColor);
		ok &= Read(n, "EmissiveStrength", m.EmissiveStrength);
		ok &= Read(n, "NormalScale", m.NormalScale);
		ok &= Read(n, "OcclusionStrength", m.OcclusionStrength);
		ok &= ReadAlphaMode(n, m.Alpha);
		ok &= Read(n, "AlphaCutoff", m.AlphaCutoff);
		ok &= Read(n, "DoubleSided", m.DoubleSided);
		ok &= Read(n, "UVTiling", m.UVTiling);
		ok &= Read(n, "BaseColorTexture", m.BaseColorTexture);
		ok &= Read(n, "MetallicRoughnessTexture", m.MetallicRoughnessTexture);
		ok &= Read(n, "NormalTexture", m.NormalTexture);
		ok &= Read(n, "OcclusionTexture", m.OcclusionTexture);
		ok &= Read(n, "EmissiveTexture", m.EmissiveTexture);
		return ok;
	}

	bool Save(const MaterialParameters& material, const std::filesystem::path& path)
	{
		YAML::Node root;
		root["Material"] = ToYAML(material);
		YAML::Emitter out;
		out << root;
		return FileSystem::WriteText(path, std::string(out.c_str()) + "\n");
	}

	std::optional<MaterialParameters> Load(const std::filesystem::path& path)
	{
		std::optional<std::string> text = FileSystem::ReadText(path);
		if (!text)
		{
			EMBER_CORE_ERROR("Material '{}' not found", path.string());
			return std::nullopt;
		}
		try
		{
			const YAML::Node root = YAML::Load(*text);
			MaterialParameters material;
			if (!root.IsMap() || !root["Material"] || !FromYAML(root["Material"], material))
			{
				EMBER_CORE_ERROR("Material '{}' is malformed", path.string());
				return std::nullopt;
			}
			return material;
		}
		catch (const YAML::Exception& e)
		{
			EMBER_CORE_ERROR("Material '{}': {}", path.string(), e.what());
			return std::nullopt;
		}
	}

}
