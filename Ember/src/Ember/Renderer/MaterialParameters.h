#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <string>

namespace Ember {

	enum class AlphaMode : uint8_t
	{
		Opaque = 0, Mask, Blend
	};

	// CPU-side description of a metallic-roughness PBR material (glTF 2.0 model).
	// Texture paths are project-asset-relative (empty = none). Factors multiply the texture values.
	struct MaterialParameters
	{
		glm::vec4 BaseColor{ 1.0f };
		float Metallic = 0.0f;
		float Roughness = 0.5f;
		glm::vec3 EmissiveColor{ 0.0f };
		float EmissiveStrength = 1.0f;
		float NormalScale = 1.0f;
		float OcclusionStrength = 1.0f;
		AlphaMode Alpha = AlphaMode::Opaque;
		float AlphaCutoff = 0.5f;
		bool DoubleSided = false;
		glm::vec2 UVTiling{ 1.0f }; // texture coordinate scale (repeat textures across large surfaces)

		std::string BaseColorTexture;
		std::string MetallicRoughnessTexture; // glTF packing: G = roughness, B = metallic
		std::string NormalTexture;
		std::string OcclusionTexture;         // R channel
		std::string EmissiveTexture;

		bool operator==(const MaterialParameters&) const = default;
	};

}
