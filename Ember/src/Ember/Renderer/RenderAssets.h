#pragma once

#include "Ember/Core/Base.h"
#include "Ember/Renderer/MaterialParameters.h"
#include "Ember/Renderer/Mesh.h"
#include "Ember/Renderer/RenderTypes.h"
#include "Ember/Renderer/ShaderLibrary.h"

#include <nvrhi/nvrhi.h>

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Ember {

	// GPU-side material: constants + binding set (descriptor set 1).
	struct GpuMaterial
	{
		MaterialParameters Parameters;
		nvrhi::BufferHandle Constants;
		nvrhi::BindingSetHandle BindingSet;
		uint64_t LastUsedFrame = 0;
	};

	// Image-based lighting maps for one sky.
	struct Environment
	{
		std::string Source; // asset path of the HDRI, or empty for the procedural sky
		nvrhi::TextureHandle EnvironmentCube;
		nvrhi::TextureHandle Irradiance;
		nvrhi::TextureHandle Prefiltered;
		uint32_t PrefilteredMipCount = 1;
	};

	// Shared GPU resources and asset caches for rendering: shaders, samplers, binding layouts, default textures,
	// models (glTF / built-in primitives), textures, materials and environments. One instance per device.
	//
	// Asset paths are relative to the project asset directory. "builtin://Cube" etc. name built-in meshes.
	// Failed loads are remembered (logged once) until ClearCaches() so a missing file never spams every frame.
	class RenderAssets
	{
	public:
		RenderAssets(nvrhi::IDevice* device, const RenderSettings& settings);
		~RenderAssets();

		RenderAssets(const RenderAssets&) = delete;
		RenderAssets& operator=(const RenderAssets&) = delete;

		void SetAssetDirectory(const std::filesystem::path& directory);
		const std::filesystem::path& GetAssetDirectory() const { return m_AssetDirectory; }

		// Returns nullptr if the model cannot be loaded.
		Ref<Model> GetModel(const std::string& path);
		// Returns nullptr if the texture cannot be loaded. Embedded glTF images use "<model path>#<index>".
		nvrhi::ITexture* GetTexture(const std::string& path, bool srgb);
		std::optional<MaterialParameters> GetMaterialAsset(const std::string& path);
		// Returns the GPU material for the given parameters (content-addressed cache).
		GpuMaterial* GetMaterial(const MaterialParameters& parameters);
		const GpuMaterial* GetDefaultMaterial() { return GetMaterial(MaterialParameters{}); }
		// Image-based lighting for an HDRI path (empty = procedural sky). Falls back to procedural on failure.
		Ref<Environment> GetEnvironment(const std::string& hdrPath);

		nvrhi::ITexture* GetBRDFLut() const { return m_BRDFLut; }
		nvrhi::ITexture* GetWhiteTexture() const { return m_WhiteTexture; }
		nvrhi::ITexture* GetBlackTexture() const { return m_BlackTexture; }

		// Drops all cached assets (e.g. after files changed on disk) and forgets failed loads.
		void ClearCaches();
		// Releases materials not used for a while. Call once per frame.
		void EndFrame();
		uint64_t GetFrameIndex() const { return m_FrameIndex; }

		nvrhi::IDevice* GetDevice() const { return m_Device; }
		ShaderLibrary& GetShaders() { return m_Shaders; }
		const RenderSettings& GetSettings() const { return m_Settings; }

		// Samplers
		nvrhi::ISampler* GetLinearWrapSampler() const { return m_LinearWrap; }
		nvrhi::ISampler* GetLinearClampSampler() const { return m_LinearClamp; }
		nvrhi::ISampler* GetPointClampSampler() const { return m_PointClamp; }
		nvrhi::ISampler* GetShadowCompareSampler() const { return m_ShadowCompare; }

		// Binding layouts (see shaders/include/Common.glsl for the slot assignments)
		nvrhi::IBindingLayout* GetFrameLayout() const { return m_FrameLayout; }
		nvrhi::IBindingLayout* GetMaterialLayout() const { return m_MaterialLayout; }
		nvrhi::IBindingLayout* GetShadowLayout() const { return m_ShadowLayout; }
		nvrhi::IInputLayout* GetMeshInputLayout() const { return m_MeshInputLayout; }
		// Position + texcoord only (shadow pass).
		nvrhi::IInputLayout* GetShadowInputLayout() const { return m_ShadowInputLayout; }

		// Helper to create layouts with Ember's conventions: zero Vulkan binding offsets, and the register space is
		// the descriptor set index (frame resources = space 0, material = space 1).
		static nvrhi::BindingLayoutDesc MakeLayoutDesc(nvrhi::ShaderType visibility, uint32_t registerSpace = 0);

		static constexpr uint32_t c_PushConstantSlot = 31;

		// Built-in mesh prefix
		static constexpr const char* c_BuiltinPrefix = "builtin://";

	private:
		Ref<Model> LoadModelFromFile(const std::string& path);
		Ref<Model> LoadGltf(const std::string& path);
		Ref<Environment> BuildEnvironment(const std::string& hdrPath);
		void CreateBRDFLut();
		void RunCompute(nvrhi::ICommandList* commandList, const char* shader, nvrhi::ITexture* input, nvrhi::TextureDimension inputDimension,
			nvrhi::ITexture* output, uint32_t outputMip, bool outputIsCube, const void* constants, uint32_t constantsSize,
			uint32_t width, uint32_t height, uint32_t layers);
		nvrhi::ComputePipelineHandle GetComputePipeline(const char* shader);
		void SubmitAndWait(nvrhi::ICommandList* commandList);

	private:
		nvrhi::IDevice* m_Device;
		RenderSettings m_Settings;
		ShaderLibrary m_Shaders;
		std::filesystem::path m_AssetDirectory;
		uint64_t m_FrameIndex = 0;

		nvrhi::SamplerHandle m_LinearWrap, m_LinearClamp, m_PointClamp, m_ShadowCompare;
		nvrhi::TextureHandle m_WhiteTexture, m_WhiteLinearTexture, m_BlackTexture, m_FlatNormalTexture, m_BRDFLut;
		nvrhi::BindingLayoutHandle m_FrameLayout, m_MaterialLayout, m_ShadowLayout, m_ComputeLayout;
		nvrhi::InputLayoutHandle m_MeshInputLayout, m_ShadowInputLayout;
		std::unordered_map<std::string, nvrhi::ComputePipelineHandle> m_ComputePipelines;

		std::unordered_map<std::string, Ref<Model>> m_Models;
		std::unordered_map<std::string, nvrhi::TextureHandle> m_Textures;
		std::unordered_map<std::string, std::vector<uint8_t>> m_EmbeddedImages; // encoded images from .glb files
		std::unordered_map<std::string, MaterialParameters> m_MaterialAssets;
		std::unordered_map<std::string, Ref<Environment>> m_Environments;
		std::unordered_multimap<size_t, Scope<GpuMaterial>> m_Materials;
		std::unordered_set<std::string> m_FailedAssets;
	};

	size_t HashMaterialParameters(const MaterialParameters& parameters);

}
