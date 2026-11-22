#include "Ember/Renderer/UIRenderer.h"

#include "Ember/Core/Assert.h"
#include "Ember/Renderer/Font.h"
#include "Ember/Scene/Components.h"
#include "Ember/Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Ember {

	namespace {

		// Must match UIConstants in shaders/UI.vert.glsl and UI.frag.glsl.
		struct UIConstants
		{
			glm::vec2 ViewportSize;
			float DistanceEdge;
			float DistanceScale;
		};
		static_assert(sizeof(UIConstants) == 16);

		// Font atlases store 128 + (128 / range) * distance (Font::c_DistanceRange); see Font.cpp.
		constexpr float c_DistanceEdge = 128.0f / 255.0f;
		constexpr float c_DistanceScale = 255.0f * static_cast<float>(Font::c_DistanceRange) / 128.0f;

		struct Element
		{
			int SortOrder = 0;
			uint64_t CreationIndex = 0;
			const UIPanelComponent* Panel = nullptr;
			const UITextComponent* Text = nullptr;
		};

	}

	UIRenderer::UIRenderer(RenderAssets& assets)
		: m_Assets(assets), m_Device(assets.GetDevice())
	{
		ShaderLibrary& shaders = assets.GetShaders();
		m_VertexShader = shaders.Get("UI", nvrhi::ShaderType::Vertex);
		m_PixelShader = shaders.Get("UI", nvrhi::ShaderType::Pixel);

		const nvrhi::VertexAttributeDesc attributes[] = {
			nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RG32_FLOAT)
				.setOffset(offsetof(Vertex, Position)).setElementStride(sizeof(Vertex)),
			nvrhi::VertexAttributeDesc().setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT)
				.setOffset(offsetof(Vertex, TexCoord)).setElementStride(sizeof(Vertex)),
			nvrhi::VertexAttributeDesc().setName("COLOR").setFormat(nvrhi::Format::RGBA32_FLOAT)
				.setOffset(offsetof(Vertex, Color)).setElementStride(sizeof(Vertex)),
			nvrhi::VertexAttributeDesc().setName("OUTLINE").setFormat(nvrhi::Format::RGBA32_FLOAT)
				.setOffset(offsetof(Vertex, OutlineColor)).setElementStride(sizeof(Vertex)),
			nvrhi::VertexAttributeDesc().setName("PARAMS").setFormat(nvrhi::Format::RGBA32_FLOAT)
				.setOffset(offsetof(Vertex, Params)).setElementStride(sizeof(Vertex)),
		};
		m_InputLayout = m_Device->createInputLayout(attributes, static_cast<uint32_t>(std::size(attributes)), m_VertexShader);

		nvrhi::BindingLayoutDesc layout = RenderAssets::MakeLayoutDesc(nvrhi::ShaderType::All);
		layout.bindings = {
			nvrhi::BindingLayoutItem::Texture_SRV(0),
			nvrhi::BindingLayoutItem::Sampler(1),
			nvrhi::BindingLayoutItem::PushConstants(RenderAssets::c_PushConstantSlot, sizeof(UIConstants)),
		};
		m_BindingLayout = m_Device->createBindingLayout(layout);
	}

	UIRenderer::~UIRenderer()
	{
		m_Device->waitForIdle();
	}

	UIRect UIRenderer::ComputeRect(const glm::vec2& anchor, const glm::vec2& offset, const glm::vec2& pivot,
		const glm::vec2& referenceSize, const glm::vec2& viewportSize)
	{
		const float scale = GetScale(viewportSize);
		const glm::vec2 size = referenceSize * scale;
		const glm::vec2 position = anchor * viewportSize + offset * scale - pivot * size;
		return UIRect{ position, position + size };
	}

	void UIRenderer::AddQuad(nvrhi::ITexture* texture, const UIRect& rect, const glm::vec2& uvMin, const glm::vec2& uvMax,
		const glm::vec4& color, const glm::vec4& outlineColor, const glm::vec4& params)
	{
		if (m_Batches.empty() || m_Batches.back().Texture != texture)
			m_Batches.push_back(Batch{ texture, static_cast<uint32_t>(m_Indices.size()), 0 });

		const uint32_t base = static_cast<uint32_t>(m_Vertices.size());
		m_Vertices.push_back(Vertex{ rect.Min, uvMin, color, outlineColor, params });
		m_Vertices.push_back(Vertex{ { rect.Max.x, rect.Min.y }, { uvMax.x, uvMin.y }, color, outlineColor, params });
		m_Vertices.push_back(Vertex{ rect.Max, uvMax, color, outlineColor, params });
		m_Vertices.push_back(Vertex{ { rect.Min.x, rect.Max.y }, { uvMin.x, uvMax.y }, color, outlineColor, params });
		for (uint32_t index : { 0u, 1u, 2u, 0u, 2u, 3u })
			m_Indices.push_back(base + index);
		m_Batches.back().IndexCount += 6;
	}

	uint32_t UIRenderer::Prepare(Scene& scene, uint32_t width, uint32_t height)
	{
		m_Vertices.clear();
		m_Indices.clear();
		m_Batches.clear();

		const glm::vec2 viewport(static_cast<float>(width), static_cast<float>(height));
		m_ViewportSize = viewport;
		if (width == 0 || height == 0)
			return 0;
		const float scale = GetScale(viewport);

		// Gather visible elements in draw order.
		std::vector<Element> elements;
		for (auto [handle, panel, id] : scene.GetAllEntitiesWith<UIPanelComponent, IDComponent>().each())
		{
			if (panel.Visible && panel.Color.a > 0.0f && panel.Size.x > 0.0f && panel.Size.y > 0.0f)
				elements.push_back(Element{ panel.SortOrder, id.CreationIndex, &panel, nullptr });
		}
		for (auto [handle, text, id] : scene.GetAllEntitiesWith<UITextComponent, IDComponent>().each())
		{
			const bool hasInk = text.Color.a > 0.0f || (text.OutlineWidth > 0.0f && text.OutlineColor.a > 0.0f);
			if (text.Visible && hasInk && !text.Text.empty() && text.FontSize > 0.0f)
				elements.push_back(Element{ text.SortOrder, id.CreationIndex, nullptr, &text });
		}
		// Ties: creation order, and an entity's panel is drawn below its own text.
		std::sort(elements.begin(), elements.end(), [](const Element& a, const Element& b)
		{
			if (a.SortOrder != b.SortOrder)
				return a.SortOrder < b.SortOrder;
			if (a.CreationIndex != b.CreationIndex)
				return a.CreationIndex < b.CreationIndex;
			return a.Panel != nullptr && b.Panel == nullptr;
		});

		uint32_t drawn = 0;
		for (const Element& element : elements)
		{
			if (const UIPanelComponent* panel = element.Panel)
			{
				nvrhi::ITexture* texture = panel->Texture.empty() ? nullptr : m_Assets.GetTexture(panel->Texture, false);
				if (!texture)
					texture = m_Assets.GetWhiteTexture();
				const UIRect rect = ComputeRect(panel->Anchor, panel->Offset, panel->Pivot, panel->Size, viewport);
				AddQuad(texture, rect, glm::vec2(0.0f), glm::vec2(1.0f), panel->Color, glm::vec4(0.0f), glm::vec4(0.0f));
				drawn++;
				continue;
			}

			const UITextComponent& text = *element.Text;
			const GpuFont* font = m_Assets.GetFont(text.Font);
			if (!font)
				continue;
			const TextLayout layout = font->Data->Layout(text.Text, text.FontSize, text.Alignment, text.LineSpacing);
			UIRect block = ComputeRect(text.Anchor, text.Offset, text.Pivot, layout.Size, viewport);
			// Snap the text origin to whole pixels so glyph edges do not shimmer while the element moves.
			const glm::vec2 origin = glm::round(block.Min);

			const float pixelsPerAtlasPixel = text.FontSize * scale / Font::c_BakeSize;
			// The outline cannot extend beyond the distance encoded around each glyph.
			const float maxOutline = static_cast<float>(Font::c_DistanceRange - 1) * pixelsPerAtlasPixel;
			const float outline = std::clamp(text.OutlineWidth * scale, 0.0f, maxOutline);
			const glm::vec4 params(1.0f, pixelsPerAtlasPixel, outline, 0.0f);
			for (const TextQuad& quad : layout.Quads)
			{
				const UIRect rect{ origin + quad.Min * scale, origin + quad.Max * scale };
				AddQuad(font->Atlas, rect, quad.UVMin, quad.UVMax, text.Color, text.OutlineColor, params);
			}
			drawn++;
		}

		std::unordered_set<nvrhi::ITexture*> used;
		for (const Batch& batch : m_Batches)
			used.insert(batch.Texture);
		std::erase_if(m_BindingSets, [&](const auto& entry) { return !used.contains(entry.first); });
		return drawn;
	}

	void UIRenderer::Render(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer)
	{
		if (m_Indices.empty())
			return;
		const nvrhi::FramebufferInfoEx& info = framebuffer->getFramebufferInfo();
		const glm::vec2 viewport(static_cast<float>(info.width), static_cast<float>(info.height));
		EMBER_CORE_ASSERT(viewport == m_ViewportSize, "UIRenderer: framebuffer size differs from the prepared size");
		if (!EnsureBufferSize(m_VertexBuffer, m_Vertices.size() * sizeof(Vertex), false) ||
			!EnsureBufferSize(m_IndexBuffer, m_Indices.size() * sizeof(uint32_t), true))
			return;
		nvrhi::IGraphicsPipeline* pipeline = GetPipeline(framebuffer);
		if (!pipeline)
			return;

		commandList->beginMarker("UI");
		commandList->writeBuffer(m_VertexBuffer, m_Vertices.data(), m_Vertices.size() * sizeof(Vertex));
		commandList->writeBuffer(m_IndexBuffer, m_Indices.data(), m_Indices.size() * sizeof(uint32_t));

		nvrhi::GraphicsState state;
		state.pipeline = pipeline;
		state.framebuffer = framebuffer;
		state.vertexBuffers = { nvrhi::VertexBufferBinding().setBuffer(m_VertexBuffer).setSlot(0).setOffset(0) };
		state.indexBuffer = nvrhi::IndexBufferBinding().setBuffer(m_IndexBuffer).setFormat(nvrhi::Format::R32_UINT).setOffset(0);
		state.viewport.addViewportAndScissorRect(nvrhi::Viewport(viewport.x, viewport.y));
		const UIConstants constants{ viewport, c_DistanceEdge, c_DistanceScale };
		for (const Batch& batch : m_Batches)
		{
			state.bindings = { GetBindingSet(batch.Texture) };
			commandList->setGraphicsState(state);
			commandList->setPushConstants(&constants, sizeof(constants));
			commandList->drawIndexed(nvrhi::DrawArguments().setVertexCount(batch.IndexCount).setStartIndexLocation(batch.FirstIndex));
		}
		commandList->endMarker();
	}

	nvrhi::IGraphicsPipeline* UIRenderer::GetPipeline(nvrhi::IFramebuffer* framebuffer)
	{
		const nvrhi::FramebufferInfo& info = framebuffer->getFramebufferInfo();
		if (m_Pipeline && info == m_PipelineFramebufferInfo)
			return m_Pipeline;

		nvrhi::GraphicsPipelineDesc desc;
		desc.VS = m_VertexShader;
		desc.PS = m_PixelShader;
		desc.inputLayout = m_InputLayout;
		desc.bindingLayouts = { m_BindingLayout };
		desc.renderState.rasterState.setCullMode(nvrhi::RasterCullMode::None);
		desc.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
		desc.renderState.blendState.targets[0].setBlendEnable(true)
			.setSrcBlend(nvrhi::BlendFactor::SrcAlpha).setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
			.setSrcBlendAlpha(nvrhi::BlendFactor::One).setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
		m_Pipeline = m_Device->createGraphicsPipeline(desc, info);
		m_PipelineFramebufferInfo = info;
		return m_Pipeline;
	}

	nvrhi::IBindingSet* UIRenderer::GetBindingSet(nvrhi::ITexture* texture)
	{
		if (auto it = m_BindingSets.find(texture); it != m_BindingSets.end())
			return it->second;
		nvrhi::BindingSetDesc desc;
		desc.bindings = {
			nvrhi::BindingSetItem::Texture_SRV(0, texture),
			nvrhi::BindingSetItem::Sampler(1, m_Assets.GetLinearClampSampler()),
			nvrhi::BindingSetItem::PushConstants(RenderAssets::c_PushConstantSlot, sizeof(UIConstants)),
		};
		nvrhi::BindingSetHandle set = m_Device->createBindingSet(desc, m_BindingLayout);
		m_BindingSets[texture] = set;
		return set;
	}

	bool UIRenderer::EnsureBufferSize(nvrhi::BufferHandle& buffer, size_t requiredSize, bool isIndexBuffer)
	{
		if (buffer && buffer->getDesc().byteSize >= requiredSize)
			return true;
		nvrhi::BufferDesc desc;
		desc.byteSize = std::max<size_t>(requiredSize + requiredSize / 2, 16 * 1024);
		desc.isVertexBuffer = !isIndexBuffer;
		desc.isIndexBuffer = isIndexBuffer;
		desc.debugName = isIndexBuffer ? "UIIndices" : "UIVertices";
		desc.initialState = isIndexBuffer ? nvrhi::ResourceStates::IndexBuffer : nvrhi::ResourceStates::VertexBuffer;
		desc.keepInitialState = true;
		buffer = m_Device->createBuffer(desc);
		return buffer != nullptr;
	}

}
