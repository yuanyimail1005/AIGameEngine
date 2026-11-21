#pragma once

#include <nvrhi/nvrhi.h>

#include <unordered_map>

struct ImDrawData;

namespace Ember {

	class ShaderLibrary;

	// Dear ImGui renderer backend for NVRHI. ImTextureID values are nvrhi::ITexture pointers.
	class ImGuiRenderer
	{
	public:
		ImGuiRenderer(nvrhi::IDevice* device, ShaderLibrary& shaders);
		~ImGuiRenderer();

		ImGuiRenderer(const ImGuiRenderer&) = delete;
		ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

		// (Re)creates the font atlas texture. Call after changing fonts.
		bool CreateFontTexture();
		// Records draw commands for the draw data into the framebuffer.
		void Render(ImDrawData* drawData, nvrhi::IFramebuffer* framebuffer);

		static void* ToTextureID(nvrhi::ITexture* texture) { return texture; }

	private:
		nvrhi::IGraphicsPipeline* GetPipeline(nvrhi::IFramebuffer* framebuffer);
		nvrhi::IBindingSet* GetBindingSet(nvrhi::ITexture* texture);
		bool EnsureBufferSize(nvrhi::BufferHandle& buffer, size_t requiredSize, bool isIndexBuffer);

	private:
		nvrhi::IDevice* m_Device;
		nvrhi::CommandListHandle m_CommandList;
		nvrhi::ShaderHandle m_VertexShader, m_PixelShader;
		nvrhi::InputLayoutHandle m_InputLayout;
		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::SamplerHandle m_Sampler;
		nvrhi::TextureHandle m_FontTexture;
		nvrhi::BufferHandle m_VertexBuffer, m_IndexBuffer;
		nvrhi::GraphicsPipelineHandle m_Pipeline;
		nvrhi::FramebufferInfo m_PipelineFramebufferInfo;
		std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> m_BindingSets;
	};

}
