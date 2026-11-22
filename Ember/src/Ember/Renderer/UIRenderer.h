#pragma once

#include "Ember/Renderer/RenderAssets.h"

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include <unordered_map>
#include <vector>

namespace Ember {

	class Scene;

	// Pixel rectangle on screen (y down).
	struct UIRect
	{
		glm::vec2 Min{ 0.0f };
		glm::vec2 Max{ 0.0f };
	};

	// Draws the screen-space UI of a scene (UIPanelComponent and UITextComponent) into an LDR framebuffer.
	// All elements are batched into one vertex buffer; consecutive elements sharing a texture share a draw call.
	class UIRenderer
	{
	public:
		explicit UIRenderer(RenderAssets& assets);
		~UIRenderer();

		UIRenderer(const UIRenderer&) = delete;
		UIRenderer& operator=(const UIRenderer&) = delete;

		// Builds the frame's UI geometry for a viewport of the given size and loads the fonts and textures it uses.
		// Must be called while no other command list is open (asset loading submits uploads). Returns the number of
		// visible elements.
		uint32_t Prepare(Scene& scene, uint32_t width, uint32_t height);
		// Records the prepared UI into an open command list; the framebuffer must match the prepared size.
		void Render(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer);

		// UI coordinates are reference pixels scaled by viewport height / c_ReferenceHeight.
		static constexpr float c_ReferenceHeight = 720.0f;
		static float GetScale(const glm::vec2& viewportSize) { return viewportSize.y / c_ReferenceHeight; }
		// Screen rectangle of an element of the given reference size (see UITextComponent for the placement rules).
		static UIRect ComputeRect(const glm::vec2& anchor, const glm::vec2& offset, const glm::vec2& pivot,
			const glm::vec2& referenceSize, const glm::vec2& viewportSize);

	private:
		struct Vertex
		{
			glm::vec2 Position;
			glm::vec2 TexCoord;
			glm::vec4 Color;
			glm::vec4 OutlineColor;
			glm::vec4 Params; // x: text flag, y: screen px per atlas px, z: outline width (px)
		};

		struct Batch
		{
			nvrhi::ITexture* Texture = nullptr;
			uint32_t FirstIndex = 0;
			uint32_t IndexCount = 0;
		};

		void AddQuad(nvrhi::ITexture* texture, const UIRect& rect, const glm::vec2& uvMin, const glm::vec2& uvMax,
			const glm::vec4& color, const glm::vec4& outlineColor, const glm::vec4& params);
		nvrhi::IGraphicsPipeline* GetPipeline(nvrhi::IFramebuffer* framebuffer);
		nvrhi::IBindingSet* GetBindingSet(nvrhi::ITexture* texture);
		bool EnsureBufferSize(nvrhi::BufferHandle& buffer, size_t requiredSize, bool isIndexBuffer);

	private:
		RenderAssets& m_Assets;
		nvrhi::IDevice* m_Device;
		nvrhi::ShaderHandle m_VertexShader, m_PixelShader;
		nvrhi::InputLayoutHandle m_InputLayout;
		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::GraphicsPipelineHandle m_Pipeline;
		nvrhi::FramebufferInfo m_PipelineFramebufferInfo;
		nvrhi::BufferHandle m_VertexBuffer, m_IndexBuffer;

		// Binding sets keep their texture alive; entries unused in a frame are dropped so released assets are freed.
		std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> m_BindingSets;

		std::vector<Vertex> m_Vertices;
		std::vector<uint32_t> m_Indices;
		std::vector<Batch> m_Batches;
		glm::vec2 m_ViewportSize{ 0.0f };
	};

}
