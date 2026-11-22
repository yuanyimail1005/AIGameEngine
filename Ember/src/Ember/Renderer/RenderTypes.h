#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace Ember {

	// Interleaved mesh vertex (48 bytes). Attribute locations: 0 position, 1 normal, 2 tangent, 3 texcoord.
	struct Vertex
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
		glm::vec4 Tangent{ 1.0f, 0.0f, 0.0f, 1.0f }; // w = bitangent sign
		glm::vec2 TexCoord{ 0.0f };
	};
	static_assert(sizeof(Vertex) == 48, "Vertex layout must match the shaders' input layout");

	struct AABB
	{
		glm::vec3 Min{ 0.0f };
		glm::vec3 Max{ 0.0f };

		glm::vec3 Center() const { return (Min + Max) * 0.5f; }
		glm::vec3 Extents() const { return (Max - Min) * 0.5f; }
		bool IsValid() const { return Min.x <= Max.x && Min.y <= Max.y && Min.z <= Max.z; }
		// Bounding box of this box after transformation.
		AABB Transformed(const glm::mat4& transform) const;
	};

	// Camera parameters for one rendered view (scene camera or editor camera).
	struct RenderCamera
	{
		glm::mat4 View{ 1.0f };
		glm::mat4 Projection{ 1.0f };
		float NearClip = 0.1f;
		float FarClip = 1000.0f;
		bool Orthographic = false;

		glm::vec3 GetPosition() const { return glm::vec3(glm::inverse(View)[3]); }
	};

	enum class TonemapOperator : int
	{
		ACES = 0, Reinhard = 1, None = 2
	};

	// Quality / feature settings. Defaults target desktop GPUs; RenderSettings::LowQuality() suits the Pi 5.
	struct RenderSettings
	{
		bool ShadowsEnabled = true;
		uint32_t ShadowMapSize = 2048;
		uint32_t ShadowCascadeCount = 3;
		float ShadowDistance = 60.0f;
		float ShadowNormalBias = 1.5f; // in shadow-map texels

		bool SSAOEnabled = true;
		float SSAORadius = 0.5f;
		float SSAOBias = 0.025f;
		float SSAOPower = 1.5f;
		float SSAOStrength = 1.0f;
		int SSAOSampleCount = 16;

		float Exposure = 1.0f;
		TonemapOperator Tonemapper = TonemapOperator::ACES;

		uint32_t EnvironmentCubeSize = 512;
		uint32_t IrradianceSize = 32;
		uint32_t PrefilteredSize = 128;
		uint32_t PrefilterSampleCount = 128;

		glm::vec3 BackgroundColor{ 0.08f, 0.08f, 0.09f }; // used when no skybox is drawn

		static RenderSettings LowQuality();
	};

	struct RenderStats
	{
		uint32_t DrawCalls = 0;
		uint32_t Triangles = 0;
		uint32_t CulledObjects = 0;
		uint32_t ShadowDrawCalls = 0;
		uint32_t UIElements = 0; // visible UI panels and texts drawn
	};

}
