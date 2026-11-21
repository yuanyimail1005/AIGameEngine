#pragma once

#include <nvrhi/nvrhi.h>

#include <unordered_map>

namespace Ember {

	class ShaderLibrary;

	// Draws a texture over a whole framebuffer (e.g. the scene image onto the swapchain), converting formats.
	class TextureBlitter
	{
	public:
		TextureBlitter(nvrhi::IDevice* device, ShaderLibrary& shaders);

		void Blit(nvrhi::ICommandList* commandList, nvrhi::ITexture* source, nvrhi::IFramebuffer* destination);

	private:
		nvrhi::IDevice* m_Device;
		nvrhi::ShaderHandle m_VertexShader, m_PixelShader;
		nvrhi::BindingLayoutHandle m_Layout;
		nvrhi::SamplerHandle m_Sampler;
		nvrhi::GraphicsPipelineHandle m_Pipeline;
		nvrhi::FramebufferInfo m_PipelineInfo;
		nvrhi::BindingSetHandle m_BindingSet;
		nvrhi::ITexture* m_BoundTexture = nullptr;
	};

}
