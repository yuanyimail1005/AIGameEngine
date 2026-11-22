#pragma once

#include "Ember/Core/Base.h"
#include "Ember/Core/UUID.h"
#include "Ember/Renderer/Image.h"
#include "Ember/Renderer/RenderAssets.h"
#include "Ember/Renderer/RenderTypes.h"
#include "Ember/Renderer/UIRenderer.h"

#include <nvrhi/nvrhi.h>

#include <functional>
#include <optional>
#include <vector>

namespace Ember {

	class Entity;
	class Scene;

	// Renders a Scene from a camera into offscreen targets:
	//   shadow cascades -> depth/normal/ID prepass -> SSAO -> forward PBR -> skybox -> transparents -> tonemap -> UI.
	// The result (GetFinalImage) is an RGBA8 image with sRGB-encoded colors, ready to display or save.
	class SceneRenderer
	{
	public:
		explicit SceneRenderer(RenderAssets& assets);
		~SceneRenderer();

		SceneRenderer(const SceneRenderer&) = delete;
		SceneRenderer& operator=(const SceneRenderer&) = delete;

		void SetViewportSize(uint32_t width, uint32_t height);
		uint32_t GetViewportWidth() const { return m_Width; }
		uint32_t GetViewportHeight() const { return m_Height; }

		// Records and submits all passes for one frame.
		void Render(Scene& scene, const RenderCamera& camera);

		nvrhi::ITexture* GetFinalImage() const { return m_LDRTarget; }
		nvrhi::ITexture* GetHDRImage() const { return m_HDRTarget; }

		// Blocking readbacks (tests, screenshots, editor picking, AI control).
		std::optional<Image> CaptureImage();
		// Entity under the given pixel of the last rendered frame (0 if none).
		UUID PickEntity(uint32_t x, uint32_t y);

		RenderSettings& GetSettings() { return m_Settings; }
		const RenderStats& GetStats() const { return m_Stats; }

		// Called after tonemapping and the screen-space UI with the LDR framebuffer (editor grid, selection outlines, gizmos).
		using OverlayCallback = std::function<void(nvrhi::ICommandList*, nvrhi::IFramebuffer*, const RenderCamera&)>;
		void SetOverlayCallback(OverlayCallback callback) { m_OverlayCallback = std::move(callback); }

		// Builds a render camera from a scene camera entity (uses the entity's world transform).
		static RenderCamera MakeCamera(Scene& scene, Entity cameraEntity);
		// Primary camera of the scene, if any.
		static std::optional<RenderCamera> GetPrimaryCamera(Scene& scene);

		nvrhi::Format GetDepthFormat() const { return m_DepthFormat; }

	private:
		struct DrawItem;
		struct FrameData;

		void CreateTargets();
		void CreateShadowTargets();
		void CreatePipelines();
		void CollectScene(Scene& scene, const RenderCamera& camera, FrameData& frame);
		void ComputeShadowCascades(const RenderCamera& camera, const glm::vec3& lightDirection, FrameData& frame);
		void UpdateFrameBindings(const Ref<Environment>& environment);

		void RenderShadows(nvrhi::ICommandList* commandList, const FrameData& frame);
		void RenderPrepass(nvrhi::ICommandList* commandList, const FrameData& frame);
		void RenderSSAO(nvrhi::ICommandList* commandList);
		void RenderForward(nvrhi::ICommandList* commandList, const FrameData& frame);
		void RenderTonemap(nvrhi::ICommandList* commandList);
		void DrawItems(nvrhi::ICommandList* commandList, const std::vector<DrawItem>& items, nvrhi::IFramebuffer* framebuffer,
			nvrhi::IGraphicsPipeline* cullBack, nvrhi::IGraphicsPipeline* cullNone, nvrhi::IBindingSet* frameSet);

	private:
		RenderAssets& m_Assets;
		nvrhi::IDevice* m_Device;
		RenderSettings m_Settings;
		RenderStats m_Stats;
		OverlayCallback m_OverlayCallback;
		Scope<UIRenderer> m_UIRenderer;

		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		nvrhi::Format m_DepthFormat = nvrhi::Format::D32;
		nvrhi::CommandListHandle m_CommandList;

		// Targets
		nvrhi::TextureHandle m_DepthTarget, m_NormalTarget, m_EntityIDTarget, m_HDRTarget, m_LDRTarget;
		nvrhi::TextureHandle m_SSAOTarget, m_SSAOBlurTarget, m_ShadowMap;
		nvrhi::FramebufferHandle m_PrepassFramebuffer, m_HDRFramebuffer, m_LDRFramebuffer, m_SSAOFramebuffer, m_SSAOBlurFramebuffer;
		std::vector<nvrhi::FramebufferHandle> m_ShadowFramebuffers;
		uint32_t m_ShadowMapSize = 0;
		uint32_t m_ShadowCascades = 0;

		// Pipelines
		nvrhi::GraphicsPipelineHandle m_ShadowPipeline;
		nvrhi::GraphicsPipelineHandle m_PrepassPipeline, m_PrepassPipelineNoCull;
		nvrhi::GraphicsPipelineHandle m_OpaquePipeline, m_OpaquePipelineNoCull;
		nvrhi::GraphicsPipelineHandle m_TransparentPipeline, m_TransparentPipelineNoCull;
		nvrhi::GraphicsPipelineHandle m_SkyboxPipeline, m_SSAOPipeline, m_BlurPipeline, m_TonemapPipeline;
		nvrhi::BindingLayoutHandle m_SSAOLayout, m_BlurLayout, m_TonemapLayout;

		// Bindings
		nvrhi::BufferHandle m_FrameConstants;
		nvrhi::BindingSetHandle m_FrameSet, m_ShadowSet, m_SSAOSet, m_BlurHorizontalSet, m_BlurVerticalSet, m_TonemapSet;
		Ref<Environment> m_BoundEnvironment;
		Ref<Environment> m_CurrentEnvironment;

		// Picking: entity ID texture stores (index + 1) into this table.
		std::vector<UUID> m_PickTable;
	};

}
