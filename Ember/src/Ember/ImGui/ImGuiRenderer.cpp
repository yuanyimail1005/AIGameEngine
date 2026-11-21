#include "Ember/ImGui/ImGuiRenderer.h"

#include "Ember/Core/Log.h"
#include "Ember/Renderer/RenderAssets.h"
#include "Ember/Renderer/ShaderLibrary.h"

#include <imgui.h>

#include <vector>

namespace Ember {

	namespace {

		struct ImGuiConstants
		{
			float Scale[2];
			float Translate[2];
		};

	}

	ImGuiRenderer::ImGuiRenderer(nvrhi::IDevice* device, ShaderLibrary& shaders)
		: m_Device(device)
	{
		m_CommandList = device->createCommandList();
		m_VertexShader = shaders.Get("ImGui", nvrhi::ShaderType::Vertex);
		m_PixelShader = shaders.Get("ImGui", nvrhi::ShaderType::Pixel);

		const nvrhi::VertexAttributeDesc attributes[] = {
			nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RG32_FLOAT)
				.setOffset(offsetof(ImDrawVert, pos)).setElementStride(sizeof(ImDrawVert)),
			nvrhi::VertexAttributeDesc().setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT)
				.setOffset(offsetof(ImDrawVert, uv)).setElementStride(sizeof(ImDrawVert)),
			nvrhi::VertexAttributeDesc().setName("COLOR").setFormat(nvrhi::Format::RGBA8_UNORM)
				.setOffset(offsetof(ImDrawVert, col)).setElementStride(sizeof(ImDrawVert)),
		};
		m_InputLayout = device->createInputLayout(attributes, 3, m_VertexShader);

		nvrhi::BindingLayoutDesc layout = RenderAssets::MakeLayoutDesc(nvrhi::ShaderType::All);
		layout.bindings = {
			nvrhi::BindingLayoutItem::Texture_SRV(0),
			nvrhi::BindingLayoutItem::Sampler(1),
			nvrhi::BindingLayoutItem::PushConstants(RenderAssets::c_PushConstantSlot, sizeof(ImGuiConstants)),
		};
		m_BindingLayout = device->createBindingLayout(layout);
		m_Sampler = device->createSampler(nvrhi::SamplerDesc().setAllAddressModes(nvrhi::SamplerAddressMode::Wrap));
	}

	ImGuiRenderer::~ImGuiRenderer()
	{
		m_Device->waitForIdle();
	}

	bool ImGuiRenderer::CreateFontTexture()
	{
		ImGuiIO& io = ImGui::GetIO();
		unsigned char* pixels = nullptr;
		int width = 0, height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		if (!pixels)
			return false;

		nvrhi::TextureDesc desc;
		desc.width = static_cast<uint32_t>(width);
		desc.height = static_cast<uint32_t>(height);
		desc.format = nvrhi::Format::RGBA8_UNORM;
		desc.debugName = "ImGuiFont";
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.keepInitialState = true;
		m_FontTexture = m_Device->createTexture(desc);
		if (!m_FontTexture)
			return false;

		m_CommandList->open();
		m_CommandList->writeTexture(m_FontTexture, 0, 0, pixels, size_t(width) * 4);
		m_CommandList->close();
		m_Device->executeCommandList(m_CommandList);

		io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(m_FontTexture.Get()));
		m_BindingSets.clear();
		return true;
	}

	nvrhi::IGraphicsPipeline* ImGuiRenderer::GetPipeline(nvrhi::IFramebuffer* framebuffer)
	{
		const nvrhi::FramebufferInfo& info = framebuffer->getFramebufferInfo();
		if (m_Pipeline && info == m_PipelineFramebufferInfo)
			return m_Pipeline;

		nvrhi::GraphicsPipelineDesc desc;
		desc.VS = m_VertexShader;
		desc.PS = m_PixelShader;
		desc.inputLayout = m_InputLayout;
		desc.bindingLayouts = { m_BindingLayout };
		desc.renderState.rasterState.setCullMode(nvrhi::RasterCullMode::None).setScissorEnable(true);
		desc.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
		desc.renderState.blendState.targets[0].setBlendEnable(true)
			.setSrcBlend(nvrhi::BlendFactor::SrcAlpha).setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
			.setSrcBlendAlpha(nvrhi::BlendFactor::One).setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
		m_Pipeline = m_Device->createGraphicsPipeline(desc, info);
		m_PipelineFramebufferInfo = info;
		return m_Pipeline;
	}

	nvrhi::IBindingSet* ImGuiRenderer::GetBindingSet(nvrhi::ITexture* texture)
	{
		if (auto it = m_BindingSets.find(texture); it != m_BindingSets.end())
			return it->second;
		nvrhi::BindingSetDesc desc;
		desc.bindings = {
			nvrhi::BindingSetItem::Texture_SRV(0, texture),
			nvrhi::BindingSetItem::Sampler(1, m_Sampler),
			nvrhi::BindingSetItem::PushConstants(RenderAssets::c_PushConstantSlot, sizeof(ImGuiConstants)),
		};
		nvrhi::BindingSetHandle set = m_Device->createBindingSet(desc, m_BindingLayout);
		m_BindingSets[texture] = set;
		return set;
	}

	bool ImGuiRenderer::EnsureBufferSize(nvrhi::BufferHandle& buffer, size_t requiredSize, bool isIndexBuffer)
	{
		if (buffer && buffer->getDesc().byteSize >= requiredSize)
			return true;
		nvrhi::BufferDesc desc;
		desc.byteSize = std::max<size_t>(requiredSize + requiredSize / 2, 64 * 1024);
		desc.isVertexBuffer = !isIndexBuffer;
		desc.isIndexBuffer = isIndexBuffer;
		desc.debugName = isIndexBuffer ? "ImGuiIndices" : "ImGuiVertices";
		desc.initialState = isIndexBuffer ? nvrhi::ResourceStates::IndexBuffer : nvrhi::ResourceStates::VertexBuffer;
		desc.keepInitialState = true;
		buffer = m_Device->createBuffer(desc);
		return buffer != nullptr;
	}

	void ImGuiRenderer::Render(ImDrawData* drawData, nvrhi::IFramebuffer* framebuffer)
	{
		if (!drawData || !framebuffer || drawData->TotalVtxCount == 0)
			return;
		const float width = drawData->DisplaySize.x * drawData->FramebufferScale.x;
		const float height = drawData->DisplaySize.y * drawData->FramebufferScale.y;
		if (width <= 0.0f || height <= 0.0f)
			return;

		if (!EnsureBufferSize(m_VertexBuffer, size_t(drawData->TotalVtxCount) * sizeof(ImDrawVert), false) ||
			!EnsureBufferSize(m_IndexBuffer, size_t(drawData->TotalIdxCount) * sizeof(ImDrawIdx), true))
			return;

		// Gather geometry into contiguous arrays.
		std::vector<ImDrawVert> vertices;
		std::vector<ImDrawIdx> indices;
		vertices.reserve(static_cast<size_t>(drawData->TotalVtxCount));
		indices.reserve(static_cast<size_t>(drawData->TotalIdxCount));
		for (int n = 0; n < drawData->CmdListsCount; n++)
		{
			const ImDrawList* list = drawData->CmdLists[n];
			vertices.insert(vertices.end(), list->VtxBuffer.begin(), list->VtxBuffer.end());
			indices.insert(indices.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());
		}

		// Binding sets hold references to textures; rebuild per frame so released textures are freed.
		nvrhi::BindingSetHandle fontSet = m_FontTexture ? GetBindingSet(m_FontTexture) : nullptr;
		m_BindingSets.clear();
		if (fontSet)
			m_BindingSets[m_FontTexture] = fontSet;

		m_CommandList->open();
		m_CommandList->beginMarker("ImGui");
		m_CommandList->writeBuffer(m_VertexBuffer, vertices.data(), vertices.size() * sizeof(ImDrawVert));
		m_CommandList->writeBuffer(m_IndexBuffer, indices.data(), indices.size() * sizeof(ImDrawIdx));

		const ImGuiConstants constants{
			{ 2.0f / drawData->DisplaySize.x, 2.0f / drawData->DisplaySize.y },
			{ -1.0f - drawData->DisplayPos.x * (2.0f / drawData->DisplaySize.x), -1.0f - drawData->DisplayPos.y * (2.0f / drawData->DisplaySize.y) } };

		nvrhi::GraphicsState state;
		state.pipeline = GetPipeline(framebuffer);
		state.framebuffer = framebuffer;
		state.vertexBuffers = { nvrhi::VertexBufferBinding().setBuffer(m_VertexBuffer).setSlot(0).setOffset(0) };
		state.indexBuffer = nvrhi::IndexBufferBinding().setBuffer(m_IndexBuffer)
			.setFormat(sizeof(ImDrawIdx) == 2 ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT).setOffset(0);

		const ImVec2 clipOffset = drawData->DisplayPos;
		const ImVec2 clipScale = drawData->FramebufferScale;
		uint32_t vertexOffset = 0;
		uint32_t indexOffset = 0;
		for (int n = 0; n < drawData->CmdListsCount; n++)
		{
			const ImDrawList* list = drawData->CmdLists[n];
			for (const ImDrawCmd& command : list->CmdBuffer)
			{
				if (command.UserCallback)
				{
					if (command.UserCallback != ImDrawCallback_ResetRenderState)
						command.UserCallback(list, &command);
					continue;
				}
				const float x0 = (command.ClipRect.x - clipOffset.x) * clipScale.x;
				const float y0 = (command.ClipRect.y - clipOffset.y) * clipScale.y;
				const float x1 = (command.ClipRect.z - clipOffset.x) * clipScale.x;
				const float y1 = (command.ClipRect.w - clipOffset.y) * clipScale.y;
				if (x1 <= x0 || y1 <= y0)
					continue;

				auto* texture = reinterpret_cast<nvrhi::ITexture*>(command.GetTexID());
				if (!texture)
					texture = m_FontTexture;
				state.bindings = { GetBindingSet(texture) };
				state.viewport = nvrhi::ViewportState()
					.addViewport(nvrhi::Viewport(width, height))
					.addScissorRect(nvrhi::Rect(static_cast<int>(std::max(x0, 0.0f)), static_cast<int>(std::min(x1, width)),
						static_cast<int>(std::max(y0, 0.0f)), static_cast<int>(std::min(y1, height))));
				m_CommandList->setGraphicsState(state);
				m_CommandList->setPushConstants(&constants, sizeof(constants));
				m_CommandList->drawIndexed(nvrhi::DrawArguments()
					.setVertexCount(command.ElemCount)
					.setStartIndexLocation(indexOffset + command.IdxOffset)
					.setStartVertexLocation(vertexOffset + command.VtxOffset));
			}
			vertexOffset += static_cast<uint32_t>(list->VtxBuffer.Size);
			indexOffset += static_cast<uint32_t>(list->IdxBuffer.Size);
		}

		m_CommandList->endMarker();
		m_CommandList->close();
		m_Device->executeCommandList(m_CommandList);
	}

}
