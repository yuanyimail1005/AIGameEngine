#include "Ember/Renderer/TextureBlitter.h"

#include "Ember/Renderer/RenderAssets.h"
#include "Ember/Renderer/ShaderLibrary.h"

namespace Ember {

	TextureBlitter::TextureBlitter(nvrhi::IDevice* device, ShaderLibrary& shaders)
		: m_Device(device)
	{
		m_VertexShader = shaders.Get("Fullscreen", nvrhi::ShaderType::Vertex);
		m_PixelShader = shaders.Get("Blit", nvrhi::ShaderType::Pixel);
		nvrhi::BindingLayoutDesc layout = RenderAssets::MakeLayoutDesc(nvrhi::ShaderType::All);
		layout.bindings = { nvrhi::BindingLayoutItem::Texture_SRV(0), nvrhi::BindingLayoutItem::Sampler(1) };
		m_Layout = device->createBindingLayout(layout);
		m_Sampler = device->createSampler(nvrhi::SamplerDesc().setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
	}

	void TextureBlitter::Blit(nvrhi::ICommandList* commandList, nvrhi::ITexture* source, nvrhi::IFramebuffer* destination)
	{
		if (!source || !destination)
			return;
		const nvrhi::FramebufferInfo& info = destination->getFramebufferInfo();
		if (!m_Pipeline || !(info == m_PipelineInfo))
		{
			nvrhi::GraphicsPipelineDesc desc;
			desc.VS = m_VertexShader;
			desc.PS = m_PixelShader;
			desc.bindingLayouts = { m_Layout };
			desc.renderState.rasterState.setCullMode(nvrhi::RasterCullMode::None);
			desc.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
			m_Pipeline = m_Device->createGraphicsPipeline(desc, info);
			m_PipelineInfo = info;
		}
		if (!m_BindingSet || m_BoundTexture != source)
		{
			nvrhi::BindingSetDesc set;
			set.bindings = { nvrhi::BindingSetItem::Texture_SRV(0, source), nvrhi::BindingSetItem::Sampler(1, m_Sampler) };
			m_BindingSet = m_Device->createBindingSet(set, m_Layout);
			m_BoundTexture = source;
		}

		const nvrhi::FramebufferInfoEx& infoEx = destination->getFramebufferInfo();
		nvrhi::GraphicsState state;
		state.pipeline = m_Pipeline;
		state.framebuffer = destination;
		state.bindings = { m_BindingSet };
		state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(infoEx.width), static_cast<float>(infoEx.height)));
		commandList->setGraphicsState(state);
		commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
	}

}
