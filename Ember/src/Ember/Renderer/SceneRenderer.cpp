#include "Ember/Renderer/SceneRenderer.h"

#include "Ember/Core/Log.h"
#include "Ember/Math/Math.h"
#include "Ember/Scene/Components.h"
#include "Ember/Scene/Entity.h"
#include "Ember/Scene/Scene.h"

#include <nvrhi/utils.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cstring>

namespace Ember {

	namespace {

		constexpr uint32_t c_MaxDirectionalLights = 4;
		constexpr uint32_t c_MaxPointLights = 32;
		constexpr uint32_t c_MaxSpotLights = 16;
		constexpr uint32_t c_MaxCascades = 4;

		struct GpuDirectionalLight
		{
			glm::vec4 Direction;
			glm::vec4 Color;
		};

		struct GpuPointLight
		{
			glm::vec4 PositionRange;
			glm::vec4 ColorIntensity;
		};

		struct GpuSpotLight
		{
			glm::vec4 PositionRange;
			glm::vec4 DirectionCos;
			glm::vec4 ColorIntensity;
			glm::vec4 Params;
		};

		// Must match FrameConstants in shaders/include/Common.glsl (std140).
		struct FrameConstants
		{
			glm::mat4 View;
			glm::mat4 Projection;
			glm::mat4 ViewProjection;
			glm::mat4 InverseView;
			glm::mat4 InverseProjection;
			glm::mat4 InverseViewProjection;
			glm::vec4 CameraPosition;
			glm::vec4 ViewportSize;
			glm::vec4 FrameParams;
			glm::vec4 SkyParams;
			glm::vec4 RenderParams;
			GpuDirectionalLight DirectionalLights[c_MaxDirectionalLights];
			GpuPointLight PointLights[c_MaxPointLights];
			GpuSpotLight SpotLights[c_MaxSpotLights];
			glm::ivec4 LightCounts;
			glm::mat4 ShadowMatrices[c_MaxCascades];
			glm::vec4 CascadeSplits;
			glm::vec4 ShadowParams;
			glm::vec4 CascadeInfo[c_MaxCascades];
		};
		static_assert(sizeof(FrameConstants) == 3008, "FrameConstants must match the std140 layout in Common.glsl");

		struct DrawConstants
		{
			glm::mat4 Model;
			glm::vec4 NormalColumns[3];
			uint32_t EntityID;
			uint32_t Flags;
		};
		static_assert(sizeof(DrawConstants) == 120, "Must match DrawConstants in Common.glsl and stay within 128 bytes");

		struct ShadowConstants
		{
			glm::mat4 Model;
			glm::mat4 LightViewProjection;
		};

		struct SSAOConstants
		{
			float Radius;
			float Bias;
			float Power;
			int32_t SampleCount;
		};

		struct BlurConstants
		{
			glm::vec2 TexelStep;
			float DepthSharpness;
			float Padding;
		};

		struct TonemapConstants
		{
			float Exposure;
			int32_t Operator;
			float Gamma;
			float Padding;
		};

		struct Frustum
		{
			std::array<glm::vec4, 6> Planes;

			// Gribb-Hartmann extraction for zero-to-one depth clip space.
			explicit Frustum(const glm::mat4& m)
			{
				const glm::mat4 t = glm::transpose(m);
				Planes[0] = t[3] + t[0];
				Planes[1] = t[3] - t[0];
				Planes[2] = t[3] + t[1];
				Planes[3] = t[3] - t[1];
				Planes[4] = t[2];
				Planes[5] = t[3] - t[2];
				for (glm::vec4& plane : Planes)
					plane /= glm::length(glm::vec3(plane));
			}

			bool Intersects(const AABB& box) const
			{
				const glm::vec3 center = box.Center();
				const glm::vec3 extents = box.Extents();
				for (const glm::vec4& plane : Planes)
				{
					const float radius = glm::dot(extents, glm::abs(glm::vec3(plane)));
					if (glm::dot(glm::vec3(plane), center) + plane.w < -radius)
						return false;
				}
				return true;
			}
		};

	}

	struct SceneRenderer::DrawItem
	{
		const Mesh* MeshPtr = nullptr;
		uint32_t SubmeshIndex = 0;
		GpuMaterial* Material = nullptr;
		glm::mat4 Transform{ 1.0f };
		glm::mat4 NormalMatrix{ 1.0f };
		uint32_t PickID = 0;
		bool DoubleSided = false;
		float DistanceToCamera = 0.0f;
	};

	struct SceneRenderer::FrameData
	{
		FrameConstants Constants{};
		std::vector<DrawItem> Opaque;
		std::vector<DrawItem> Transparent;
		std::vector<DrawItem> ShadowCasters;
		glm::mat4 ShadowMatrices[c_MaxCascades]{};
		uint32_t CascadeCount = 0;
		bool DrawSkybox = true;
		Ref<Environment> EnvironmentMaps;
	};

	SceneRenderer::SceneRenderer(RenderAssets& assets)
		: m_Assets(assets), m_Device(assets.GetDevice()), m_Settings(assets.GetSettings())
	{
		// Depth format: D32 with D16 fallback. Depth is read with point sampling (SSAO, shadow blocker search) or
		// comparison samplers only, so linear filtering support is not required.
		const nvrhi::FormatSupport required = nvrhi::FormatSupport::DepthStencil | nvrhi::FormatSupport::ShaderLoad;
		if ((m_Device->queryFormatSupport(nvrhi::Format::D32) & required) == required)
			m_DepthFormat = nvrhi::Format::D32;
		else
			m_DepthFormat = nvrhi::Format::D16;

		m_CommandList = m_Device->createCommandList();
		m_UIRenderer = CreateScope<UIRenderer>(assets);
		m_FrameConstants = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(FrameConstants), "FrameConstants", 16));

		auto makeLayout = [&](std::initializer_list<nvrhi::BindingLayoutItem> items)
		{
			nvrhi::BindingLayoutDesc desc = RenderAssets::MakeLayoutDesc(nvrhi::ShaderType::All);
			desc.bindings = items;
			return m_Device->createBindingLayout(desc);
		};
		m_SSAOLayout = makeLayout({
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), nvrhi::BindingLayoutItem::Texture_SRV(1),
			nvrhi::BindingLayoutItem::Texture_SRV(2), nvrhi::BindingLayoutItem::Sampler(3),
			nvrhi::BindingLayoutItem::PushConstants(RenderAssets::c_PushConstantSlot, sizeof(SSAOConstants)) });
		m_BlurLayout = makeLayout({
			nvrhi::BindingLayoutItem::Texture_SRV(1), nvrhi::BindingLayoutItem::Texture_SRV(2), nvrhi::BindingLayoutItem::Sampler(3),
			nvrhi::BindingLayoutItem::PushConstants(RenderAssets::c_PushConstantSlot, sizeof(BlurConstants)) });
		{
			nvrhi::BindingSetDesc shadowSet;
			shadowSet.bindings = { nvrhi::BindingSetItem::PushConstants(RenderAssets::c_PushConstantSlot, sizeof(ShadowConstants)) };
			m_ShadowSet = m_Device->createBindingSet(shadowSet, m_Assets.GetShadowLayout());
		}
		m_TonemapLayout = makeLayout({
			nvrhi::BindingLayoutItem::Texture_SRV(0), nvrhi::BindingLayoutItem::Sampler(1),
			nvrhi::BindingLayoutItem::PushConstants(RenderAssets::c_PushConstantSlot, sizeof(TonemapConstants)) });

		SetViewportSize(1280, 720);
	}

	SceneRenderer::~SceneRenderer()
	{
		m_Device->waitForIdle();
	}

	void SceneRenderer::SetViewportSize(uint32_t width, uint32_t height)
	{
		width = std::max(width, 1u);
		height = std::max(height, 1u);
		if (width == m_Width && height == m_Height && m_LDRTarget)
			return;
		m_Width = width;
		m_Height = height;
		m_Device->waitForIdle();
		CreateTargets();
		if (!m_TonemapPipeline)
			CreatePipelines();
		m_BoundEnvironment = nullptr; // frame bindings reference the SSAO target
	}

	void SceneRenderer::CreateTargets()
	{
		auto makeTarget = [&](nvrhi::Format format, const char* name, bool depth = false)
		{
			nvrhi::TextureDesc desc;
			desc.width = m_Width;
			desc.height = m_Height;
			desc.format = format;
			desc.isRenderTarget = true;
			desc.debugName = name;
			desc.initialState = depth ? nvrhi::ResourceStates::DepthWrite : nvrhi::ResourceStates::RenderTarget;
			desc.keepInitialState = true;
			desc.setClearValue(depth ? nvrhi::Color(1.0f) : nvrhi::Color(0.0f));
			return m_Device->createTexture(desc);
		};

		m_DepthTarget = makeTarget(m_DepthFormat, "SceneDepth", true);
		m_NormalTarget = makeTarget(nvrhi::Format::RGBA8_UNORM, "SceneNormals");
		m_EntityIDTarget = makeTarget(nvrhi::Format::R32_UINT, "EntityIDs");
		m_HDRTarget = makeTarget(nvrhi::Format::RGBA16_FLOAT, "SceneHDR");
		m_LDRTarget = makeTarget(nvrhi::Format::RGBA8_UNORM, "SceneLDR");
		m_SSAOTarget = makeTarget(nvrhi::Format::R8_UNORM, "SSAO");
		m_SSAOBlurTarget = makeTarget(nvrhi::Format::R8_UNORM, "SSAOBlur");

		m_PrepassFramebuffer = m_Device->createFramebuffer(nvrhi::FramebufferDesc()
			.addColorAttachment(m_NormalTarget).addColorAttachment(m_EntityIDTarget).setDepthAttachment(m_DepthTarget));
		m_HDRFramebuffer = m_Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_HDRTarget).setDepthAttachment(m_DepthTarget));
		m_LDRFramebuffer = m_Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_LDRTarget));
		m_SSAOFramebuffer = m_Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_SSAOTarget));
		m_SSAOBlurFramebuffer = m_Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_SSAOBlurTarget));

		nvrhi::BindingSetDesc ssaoSet;
		ssaoSet.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, m_FrameConstants),
			nvrhi::BindingSetItem::Texture_SRV(1, m_DepthTarget),
			nvrhi::BindingSetItem::Texture_SRV(2, m_NormalTarget),
			nvrhi::BindingSetItem::Sampler(3, m_Assets.GetPointClampSampler()),
			nvrhi::BindingSetItem::PushConstants(RenderAssets::c_PushConstantSlot, sizeof(SSAOConstants)),
		};
		m_SSAOSet = m_Device->createBindingSet(ssaoSet, m_SSAOLayout);

		auto blurSet = [&](nvrhi::ITexture* input)
		{
			nvrhi::BindingSetDesc desc;
			desc.bindings = {
				nvrhi::BindingSetItem::Texture_SRV(1, input),
				nvrhi::BindingSetItem::Texture_SRV(2, m_DepthTarget),
				nvrhi::BindingSetItem::Sampler(3, m_Assets.GetPointClampSampler()),
				nvrhi::BindingSetItem::PushConstants(RenderAssets::c_PushConstantSlot, sizeof(BlurConstants)),
			};
			return m_Device->createBindingSet(desc, m_BlurLayout);
		};
		m_BlurHorizontalSet = blurSet(m_SSAOTarget);
		m_BlurVerticalSet = blurSet(m_SSAOBlurTarget);

		nvrhi::BindingSetDesc tonemapSet;
		tonemapSet.bindings = {
			nvrhi::BindingSetItem::Texture_SRV(0, m_HDRTarget),
			nvrhi::BindingSetItem::Sampler(1, m_Assets.GetLinearClampSampler()),
			nvrhi::BindingSetItem::PushConstants(RenderAssets::c_PushConstantSlot, sizeof(TonemapConstants)),
		};
		m_TonemapSet = m_Device->createBindingSet(tonemapSet, m_TonemapLayout);

		CreateShadowTargets();
	}

	void SceneRenderer::CreateShadowTargets()
	{
		const uint32_t size = std::clamp(m_Settings.ShadowMapSize, 256u, 8192u);
		const uint32_t cascades = std::clamp(m_Settings.ShadowCascadeCount, 1u, c_MaxCascades);
		if (m_ShadowMap && size == m_ShadowMapSize && cascades == m_ShadowCascades)
			return;
		m_ShadowMapSize = size;
		m_ShadowCascades = cascades;

		nvrhi::TextureDesc desc;
		desc.width = size;
		desc.height = size;
		desc.arraySize = cascades;
		desc.dimension = nvrhi::TextureDimension::Texture2DArray;
		desc.format = m_DepthFormat;
		desc.isRenderTarget = true;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.keepInitialState = true;
		desc.setClearValue(nvrhi::Color(1.0f));
		desc.debugName = "ShadowCascades";
		m_ShadowMap = m_Device->createTexture(desc);

		m_ShadowFramebuffers.clear();
		for (uint32_t i = 0; i < cascades; i++)
		{
			m_ShadowFramebuffers.push_back(m_Device->createFramebuffer(nvrhi::FramebufferDesc()
				.setDepthAttachment(m_ShadowMap, nvrhi::TextureSubresourceSet(0, 1, i, 1))));
		}
		m_BoundEnvironment = nullptr;
	}

	void SceneRenderer::CreatePipelines()
	{
		ShaderLibrary& shaders = m_Assets.GetShaders();
		nvrhi::ShaderHandle meshVS = shaders.Get("Mesh", nvrhi::ShaderType::Vertex);
		nvrhi::ShaderHandle fullscreenVS = shaders.Get("Fullscreen", nvrhi::ShaderType::Vertex);

		auto meshPipeline = [&](nvrhi::IShader* ps, nvrhi::IFramebuffer* framebuffer, bool cullBack, bool depthWrite,
			nvrhi::ComparisonFunc depthFunc, bool blend)
		{
			nvrhi::GraphicsPipelineDesc desc;
			desc.VS = meshVS;
			desc.PS = ps;
			desc.inputLayout = m_Assets.GetMeshInputLayout();
			desc.bindingLayouts = { m_Assets.GetFrameLayout(), m_Assets.GetMaterialLayout() };
			desc.primType = nvrhi::PrimitiveType::TriangleList;
			desc.renderState.rasterState.setCullMode(cullBack ? nvrhi::RasterCullMode::Back : nvrhi::RasterCullMode::None)
				.setFrontCounterClockwise(true);
			desc.renderState.depthStencilState.setDepthTestEnable(true).setDepthWriteEnable(depthWrite).setDepthFunc(depthFunc);
			if (blend)
			{
				desc.renderState.blendState.targets[0].setBlendEnable(true)
					.setSrcBlend(nvrhi::BlendFactor::SrcAlpha).setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
					.setSrcBlendAlpha(nvrhi::BlendFactor::One).setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
			}
			return m_Device->createGraphicsPipeline(desc, framebuffer->getFramebufferInfo());
		};

		nvrhi::ShaderHandle prepassPS = shaders.Get("DepthNormal", nvrhi::ShaderType::Pixel);
		nvrhi::ShaderHandle pbrPS = shaders.Get("PBR", nvrhi::ShaderType::Pixel);
		m_PrepassPipeline = meshPipeline(prepassPS, m_PrepassFramebuffer, true, true, nvrhi::ComparisonFunc::LessOrEqual, false);
		m_PrepassPipelineNoCull = meshPipeline(prepassPS, m_PrepassFramebuffer, false, true, nvrhi::ComparisonFunc::LessOrEqual, false);
		m_OpaquePipeline = meshPipeline(pbrPS, m_HDRFramebuffer, true, false, nvrhi::ComparisonFunc::LessOrEqual, false);
		m_OpaquePipelineNoCull = meshPipeline(pbrPS, m_HDRFramebuffer, false, false, nvrhi::ComparisonFunc::LessOrEqual, false);
		m_TransparentPipeline = meshPipeline(pbrPS, m_HDRFramebuffer, true, false, nvrhi::ComparisonFunc::LessOrEqual, true);
		m_TransparentPipelineNoCull = meshPipeline(pbrPS, m_HDRFramebuffer, false, false, nvrhi::ComparisonFunc::LessOrEqual, true);

		// Shadows: no culling (single-sided geometry still casts), slope-scaled bias, depth clamping ("pancaking").
		{
			nvrhi::GraphicsPipelineDesc desc;
			desc.VS = shaders.Get("Shadow", nvrhi::ShaderType::Vertex);
			desc.PS = shaders.Get("Shadow", nvrhi::ShaderType::Pixel);
			desc.inputLayout = m_Assets.GetShadowInputLayout();
			desc.bindingLayouts = { m_Assets.GetShadowLayout(), m_Assets.GetMaterialLayout() };
			desc.renderState.rasterState.setCullMode(nvrhi::RasterCullMode::None).setDepthClipEnable(false)
				.setDepthBias(1).setSlopeScaleDepthBias(1.5f);
			desc.renderState.depthStencilState.setDepthTestEnable(true).setDepthWriteEnable(true).setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual);
			m_ShadowPipeline = m_Device->createGraphicsPipeline(desc, m_ShadowFramebuffers[0]->getFramebufferInfo());
		}

		// Skybox: drawn at the far plane where nothing else was rendered.
		{
			nvrhi::GraphicsPipelineDesc desc;
			desc.VS = shaders.Get("Skybox", nvrhi::ShaderType::Vertex);
			desc.PS = shaders.Get("Skybox", nvrhi::ShaderType::Pixel);
			desc.bindingLayouts = { m_Assets.GetFrameLayout() };
			desc.renderState.rasterState.setCullMode(nvrhi::RasterCullMode::None);
			desc.renderState.depthStencilState.setDepthTestEnable(true).setDepthWriteEnable(false).setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual);
			m_SkyboxPipeline = m_Device->createGraphicsPipeline(desc, m_HDRFramebuffer->getFramebufferInfo());
		}

		auto fullscreenPipeline = [&](const char* ps, nvrhi::IBindingLayout* layout, nvrhi::IFramebuffer* framebuffer)
		{
			nvrhi::GraphicsPipelineDesc desc;
			desc.VS = fullscreenVS;
			desc.PS = shaders.Get(ps, nvrhi::ShaderType::Pixel);
			desc.bindingLayouts = { layout };
			desc.renderState.rasterState.setCullMode(nvrhi::RasterCullMode::None);
			desc.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
			return m_Device->createGraphicsPipeline(desc, framebuffer->getFramebufferInfo());
		};
		m_SSAOPipeline = fullscreenPipeline("SSAO", m_SSAOLayout, m_SSAOFramebuffer);
		m_BlurPipeline = fullscreenPipeline("SSAOBlur", m_BlurLayout, m_SSAOFramebuffer);
		m_TonemapPipeline = fullscreenPipeline("Tonemap", m_TonemapLayout, m_LDRFramebuffer);
	}

	void SceneRenderer::UpdateFrameBindings(const Ref<Environment>& environment)
	{
		if (m_FrameSet && m_BoundEnvironment == environment)
			return;
		nvrhi::BindingSetDesc desc;
		desc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, m_FrameConstants),
			nvrhi::BindingSetItem::Texture_SRV(1, m_ShadowMap, nvrhi::Format::UNKNOWN, nvrhi::AllSubresources, nvrhi::TextureDimension::Texture2DArray),
			nvrhi::BindingSetItem::Texture_SRV(2, environment->Irradiance, nvrhi::Format::UNKNOWN, nvrhi::AllSubresources, nvrhi::TextureDimension::TextureCube),
			nvrhi::BindingSetItem::Texture_SRV(3, environment->Prefiltered, nvrhi::Format::UNKNOWN, nvrhi::AllSubresources, nvrhi::TextureDimension::TextureCube),
			nvrhi::BindingSetItem::Texture_SRV(4, m_Assets.GetBRDFLut()),
			nvrhi::BindingSetItem::Texture_SRV(5, m_SSAOTarget),
			nvrhi::BindingSetItem::Sampler(6, m_Assets.GetLinearClampSampler()),
			nvrhi::BindingSetItem::Sampler(7, m_Assets.GetShadowCompareSampler()),
			nvrhi::BindingSetItem::Texture_SRV(8, environment->EnvironmentCube, nvrhi::Format::UNKNOWN, nvrhi::AllSubresources, nvrhi::TextureDimension::TextureCube),
			nvrhi::BindingSetItem::Sampler(9, m_Assets.GetPointClampSampler()),
			nvrhi::BindingSetItem::PushConstants(RenderAssets::c_PushConstantSlot, sizeof(DrawConstants)),
		};
		m_FrameSet = m_Device->createBindingSet(desc, m_Assets.GetFrameLayout());
		m_BoundEnvironment = environment;
	}

	RenderCamera SceneRenderer::MakeCamera(Scene& scene, Entity cameraEntity)
	{
		RenderCamera camera;
		if (!cameraEntity || !cameraEntity.HasComponent<CameraComponent>())
			return camera;
		glm::vec3 position, scale;
		glm::quat rotation;
		Math::DecomposeTransform(scene.GetWorldTransform(cameraEntity), position, rotation, scale);
		camera.View = glm::inverse(Math::ComposeTransform(position, rotation, glm::vec3(1.0f)));
		const SceneCamera& sceneCamera = cameraEntity.GetComponent<CameraComponent>().Camera;
		camera.Projection = sceneCamera.GetProjection();
		camera.Orthographic = sceneCamera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic;
		camera.NearClip = camera.Orthographic ? sceneCamera.GetOrthographicNearClip() : sceneCamera.GetPerspectiveNearClip();
		camera.FarClip = camera.Orthographic ? sceneCamera.GetOrthographicFarClip() : sceneCamera.GetPerspectiveFarClip();
		return camera;
	}

	std::optional<RenderCamera> SceneRenderer::GetPrimaryCamera(Scene& scene)
	{
		Entity camera = scene.GetPrimaryCameraEntity();
		if (!camera)
			return std::nullopt;
		return MakeCamera(scene, camera);
	}

	void SceneRenderer::CollectScene(Scene& scene, const RenderCamera& camera, FrameData& frame)
	{
		FrameConstants& c = frame.Constants;
		const glm::vec3 cameraPosition = camera.GetPosition();
		const glm::mat4 viewProjection = camera.Projection * camera.View;
		const Frustum frustum(viewProjection);

		// --- Lights
		struct LightCandidate
		{
			Entity Handle;
			float Distance;
		};
		uint32_t directionalCount = 0;
		bool shadowCasterAssigned = false;
		glm::vec3 shadowDirection(0.0f, -1.0f, 0.0f);
		float shadowSoftness = 1.0f;
		std::vector<LightCandidate> points, spots;
		for (Entity entity : scene.GetAllEntitiesOrdered())
		{
			const glm::mat4 world = scene.GetWorldTransform(entity);
			if (const auto* light = entity.TryGetComponent<DirectionalLightComponent>(); light && directionalCount < c_MaxDirectionalLights)
			{
				// Lights shine along the entity's -Z axis (like cameras).
				const glm::vec3 direction = glm::normalize(-glm::vec3(world[2]));
				const bool castsShadows = light->CastShadows && !shadowCasterAssigned && m_Settings.ShadowsEnabled;
				if (castsShadows)
				{
					shadowCasterAssigned = true;
					shadowDirection = direction;
					shadowSoftness = std::max(light->ShadowSoftness, 0.0f);
				}
				c.DirectionalLights[directionalCount++] = { glm::vec4(direction, std::max(light->Intensity, 0.0f)),
					glm::vec4(light->Color, castsShadows ? 1.0f : 0.0f) };
			}
			if (entity.HasComponent<PointLightComponent>())
				points.push_back({ entity, glm::distance(glm::vec3(world[3]), cameraPosition) });
			if (entity.HasComponent<SpotLightComponent>())
				spots.push_back({ entity, glm::distance(glm::vec3(world[3]), cameraPosition) });
		}
		auto byDistance = [](const LightCandidate& a, const LightCandidate& b) { return a.Distance < b.Distance; };
		std::stable_sort(points.begin(), points.end(), byDistance);
		std::stable_sort(spots.begin(), spots.end(), byDistance);

		uint32_t pointCount = 0;
		for (const LightCandidate& candidate : points)
		{
			if (pointCount >= c_MaxPointLights)
				break;
			const auto& light = candidate.Handle.GetComponent<PointLightComponent>();
			const glm::vec3 position(scene.GetWorldTransform(candidate.Handle)[3]);
			c.PointLights[pointCount++] = { glm::vec4(position, std::max(light.Range, 0.01f)), glm::vec4(light.Color, std::max(light.Intensity, 0.0f)) };
		}
		uint32_t spotCount = 0;
		for (const LightCandidate& candidate : spots)
		{
			if (spotCount >= c_MaxSpotLights)
				break;
			const auto& light = candidate.Handle.GetComponent<SpotLightComponent>();
			const glm::mat4 world = scene.GetWorldTransform(candidate.Handle);
			const float outer = std::clamp(light.OuterConeAngle, 0.01f, 1.55f);
			const float inner = std::clamp(light.InnerConeAngle, 0.0f, outer - 0.001f);
			c.SpotLights[spotCount++] = {
				glm::vec4(glm::vec3(world[3]), std::max(light.Range, 0.01f)),
				glm::vec4(glm::normalize(-glm::vec3(world[2])), std::cos(outer)),
				glm::vec4(light.Color, std::max(light.Intensity, 0.0f)),
				glm::vec4(std::cos(inner), 0.0f, 0.0f, 0.0f) };
		}
		c.LightCounts = glm::ivec4(directionalCount, pointCount, spotCount, 0);

		// --- Sky
		std::string environmentPath;
		float skyIntensity = 1.0f;
		float skyRotation = 0.0f;
		frame.DrawSkybox = true;
		{
			Entity sky;
			uint64_t bestIndex = UINT64_MAX;
			auto view = scene.GetAllEntitiesWith<SkyLightComponent, IDComponent>();
			for (entt::entity handle : view)
			{
				const uint64_t index = view.get<IDComponent>(handle).CreationIndex;
				if (index < bestIndex)
				{
					bestIndex = index;
					sky = Entity(handle, &scene);
				}
			}
			if (sky)
			{
				const auto& component = sky.GetComponent<SkyLightComponent>();
				environmentPath = component.EnvironmentMap;
				skyIntensity = std::max(component.Intensity, 0.0f);
				skyRotation = component.Rotation;
				frame.DrawSkybox = component.DrawSkybox;
			}
		}
		frame.EnvironmentMaps = m_Assets.GetEnvironment(environmentPath);
		c.SkyParams = glm::vec4(skyIntensity, skyRotation, static_cast<float>(frame.EnvironmentMaps->PrefilteredMipCount), frame.DrawSkybox ? 1.0f : 0.0f);

		// --- Draw items
		m_PickTable.clear();
		auto view = scene.GetAllEntitiesWith<MeshRendererComponent>();
		std::vector<Entity> renderables;
		for (entt::entity handle : view)
			renderables.emplace_back(handle, &scene);
		// Deterministic order (creation order) so picking IDs and draw order are stable.
		std::sort(renderables.begin(), renderables.end(), [](Entity a, Entity b)
		{
			return a.GetComponent<IDComponent>().CreationIndex < b.GetComponent<IDComponent>().CreationIndex;
		});

		for (Entity entity : renderables)
		{
			const auto& renderer = entity.GetComponent<MeshRendererComponent>();
			if (!renderer.Visible || renderer.Mesh.empty())
				continue;
			Ref<Model> model = m_Assets.GetModel(renderer.Mesh);
			if (!model)
				continue;

			const glm::mat4 world = scene.GetWorldTransform(entity);
			const glm::mat4 normalMatrix = glm::transpose(glm::inverse(world));
			const AABB worldBounds = model->MeshData->Bounds.Transformed(world);

			std::optional<MaterialParameters> assetMaterial;
			if (!renderer.OverrideMaterial && !renderer.Material.empty())
				assetMaterial = m_Assets.GetMaterialAsset(renderer.Material);

			m_PickTable.push_back(entity.GetUUID());
			const uint32_t pickID = static_cast<uint32_t>(m_PickTable.size());
			const bool visible = frustum.Intersects(worldBounds);
			if (!visible)
				m_Stats.CulledObjects++;

			for (uint32_t s = 0; s < model->MeshData->Submeshes.size(); s++)
			{
				const Submesh& submesh = model->MeshData->Submeshes[s];
				const MaterialParameters* parameters = nullptr;
				if (renderer.OverrideMaterial)
					parameters = &renderer.MaterialOverride;
				else if (assetMaterial)
					parameters = &*assetMaterial;
				else if (submesh.MaterialIndex < model->Materials.size())
					parameters = &model->Materials[submesh.MaterialIndex];
				const MaterialParameters fallback;
				GpuMaterial* material = m_Assets.GetMaterial(parameters ? *parameters : fallback);

				DrawItem item;
				item.MeshPtr = model->MeshData.get();
				item.SubmeshIndex = s;
				item.Material = material;
				item.Transform = world;
				item.NormalMatrix = normalMatrix;
				item.PickID = pickID;
				item.DoubleSided = material->Parameters.DoubleSided;
				item.DistanceToCamera = glm::distance(glm::vec3(world * glm::vec4(submesh.Bounds.Center(), 1.0f)), cameraPosition);

				if (renderer.CastShadows && material->Parameters.Alpha != AlphaMode::Blend)
					frame.ShadowCasters.push_back(item);
				if (!visible)
					continue;
				if (material->Parameters.Alpha == AlphaMode::Blend)
					frame.Transparent.push_back(item);
				else
					frame.Opaque.push_back(item);
			}
		}

		// Opaque: group by material to minimize state changes. Transparent: back to front.
		std::stable_sort(frame.Opaque.begin(), frame.Opaque.end(), [](const DrawItem& a, const DrawItem& b)
		{
			if (a.DoubleSided != b.DoubleSided)
				return a.DoubleSided < b.DoubleSided;
			return a.Material < b.Material;
		});
		std::stable_sort(frame.Transparent.begin(), frame.Transparent.end(), [](const DrawItem& a, const DrawItem& b)
		{
			return a.DistanceToCamera > b.DistanceToCamera;
		});

		// --- Shadows
		frame.CascadeCount = 0;
		if (shadowCasterAssigned)
			ComputeShadowCascades(camera, shadowDirection, frame);
		for (uint32_t i = 0; i < c_MaxCascades; i++)
			c.ShadowMatrices[i] = frame.ShadowMatrices[i];
		c.ShadowParams = glm::vec4(static_cast<float>(frame.CascadeCount), 0.0f, static_cast<float>(m_ShadowMapSize), m_Settings.ShadowNormalBias);
		c.ShadowParams.y = shadowSoftness;

		// --- Camera and global parameters
		c.View = camera.View;
		c.Projection = camera.Projection;
		c.ViewProjection = viewProjection;
		c.InverseView = glm::inverse(camera.View);
		c.InverseProjection = glm::inverse(camera.Projection);
		c.InverseViewProjection = glm::inverse(viewProjection);
		c.CameraPosition = glm::vec4(cameraPosition, 1.0f);
		c.ViewportSize = glm::vec4(static_cast<float>(m_Width), static_cast<float>(m_Height), 1.0f / static_cast<float>(m_Width), 1.0f / static_cast<float>(m_Height));
		c.FrameParams = glm::vec4(static_cast<float>(scene.GetRuntimeTime()), camera.NearClip, camera.FarClip, m_Settings.SSAOEnabled ? 1.0f : 0.0f);
		c.RenderParams = glm::vec4(m_Settings.Exposure, std::clamp(m_Settings.SSAOStrength, 0.0f, 1.0f), camera.Orthographic ? 1.0f : 0.0f, 0.0f);
	}

	void SceneRenderer::ComputeShadowCascades(const RenderCamera& camera, const glm::vec3& lightDirection, FrameData& frame)
	{
		const uint32_t cascades = m_ShadowCascades;
		const float nearClip = std::max(camera.Orthographic ? 0.0f : camera.NearClip, 0.0f);
		const float farClip = std::max(std::min(camera.FarClip, m_Settings.ShadowDistance), nearClip + 0.1f);

		// Practical split scheme (blend of logarithmic and uniform).
		constexpr float lambda = 0.75f;
		float splits[c_MaxCascades]{};
		for (uint32_t i = 0; i < cascades; i++)
		{
			const float p = static_cast<float>(i + 1) / static_cast<float>(cascades);
			const float logSplit = std::max(nearClip, 0.01f) * std::pow(farClip / std::max(nearClip, 0.01f), p);
			const float uniformSplit = nearClip + (farClip - nearClip) * p;
			splits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
		}

		// Frustum corners in view space at the camera's near and far planes.
		const glm::mat4 inverseProjection = glm::inverse(camera.Projection);
		const glm::mat4 inverseView = glm::inverse(camera.View);
		glm::vec3 nearCorners[4], farCorners[4];
		const glm::vec2 ndc[4] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
		for (int i = 0; i < 4; i++)
		{
			glm::vec4 n = inverseProjection * glm::vec4(ndc[i], 0.0f, 1.0f);
			glm::vec4 f = inverseProjection * glm::vec4(ndc[i], 1.0f, 1.0f);
			nearCorners[i] = glm::vec3(n) / n.w;
			farCorners[i] = glm::vec3(f) / f.w;
		}
		const float cameraNear = -nearCorners[0].z;
		const float cameraFar = -farCorners[0].z;
		auto cornerAt = [&](int i, float distance)
		{
			const float t = (distance - cameraNear) / std::max(cameraFar - cameraNear, 1e-4f);
			return glm::mix(nearCorners[i], farCorners[i], t);
		};

		const glm::vec3 up = std::abs(lightDirection.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
		constexpr float casterExtent = 60.0f; // shadow casters behind the slice

		float previous = nearClip;
		for (uint32_t c = 0; c < cascades; c++)
		{
			glm::vec3 corners[8];
			for (int i = 0; i < 4; i++)
			{
				corners[i] = glm::vec3(inverseView * glm::vec4(cornerAt(i, previous), 1.0f));
				corners[i + 4] = glm::vec3(inverseView * glm::vec4(cornerAt(i, splits[c]), 1.0f));
			}
			glm::vec3 center(0.0f);
			for (const glm::vec3& corner : corners)
				center += corner;
			center /= 8.0f;
			float radius = 0.0f;
			for (const glm::vec3& corner : corners)
				radius = std::max(radius, glm::distance(corner, center));
			radius = std::ceil(radius * 16.0f) / 16.0f;

			const glm::mat4 lightView = glm::lookAt(center - lightDirection * (radius + casterExtent), center, up);
			glm::mat4 lightProjection = glm::ortho(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + casterExtent);

			// Snap to texels so shadows do not shimmer when the camera moves.
			const glm::mat4 shadowMatrix = lightProjection * lightView;
			glm::vec4 origin = shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
			origin *= static_cast<float>(m_ShadowMapSize) * 0.5f;
			const glm::vec4 rounded = glm::round(origin);
			glm::vec4 offset = (rounded - origin) * (2.0f / static_cast<float>(m_ShadowMapSize));
			lightProjection[3][0] += offset.x;
			lightProjection[3][1] += offset.y;

			frame.ShadowMatrices[c] = lightProjection * lightView;
			frame.Constants.CascadeSplits[static_cast<int>(c)] = splits[c];
			frame.Constants.CascadeInfo[c] = glm::vec4(2.0f * radius, 2.0f * radius + casterExtent, 0.0f, 0.0f);
			previous = splits[c];
		}
		frame.CascadeCount = cascades;
	}

	void SceneRenderer::DrawItems(nvrhi::ICommandList* commandList, const std::vector<DrawItem>& items, nvrhi::IFramebuffer* framebuffer,
		nvrhi::IGraphicsPipeline* cullBack, nvrhi::IGraphicsPipeline* cullNone, nvrhi::IBindingSet* frameSet)
	{
		nvrhi::GraphicsState state;
		state.framebuffer = framebuffer;
		state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(m_Width), static_cast<float>(m_Height)));
		for (const DrawItem& item : items)
		{
			const Submesh& submesh = item.MeshPtr->Submeshes[item.SubmeshIndex];
			state.pipeline = item.DoubleSided ? cullNone : cullBack;
			state.bindings = { frameSet, item.Material->BindingSet };
			state.vertexBuffers = { nvrhi::VertexBufferBinding().setBuffer(item.MeshPtr->VertexBuffer).setSlot(0).setOffset(0) };
			state.indexBuffer = nvrhi::IndexBufferBinding().setBuffer(item.MeshPtr->IndexBuffer).setFormat(nvrhi::Format::R32_UINT).setOffset(0);
			commandList->setGraphicsState(state);

			DrawConstants constants{ item.Transform,
				{ glm::vec4(glm::vec3(item.NormalMatrix[0]), 0.0f), glm::vec4(glm::vec3(item.NormalMatrix[1]), 0.0f), glm::vec4(glm::vec3(item.NormalMatrix[2]), 0.0f) },
				item.PickID, 0u };
			commandList->setPushConstants(&constants, sizeof(constants));
			commandList->drawIndexed(nvrhi::DrawArguments().setVertexCount(submesh.IndexCount).setStartIndexLocation(submesh.FirstIndex));
			m_Stats.DrawCalls++;
			m_Stats.Triangles += submesh.IndexCount / 3;
		}
	}

	void SceneRenderer::RenderShadows(nvrhi::ICommandList* commandList, const FrameData& frame)
	{
		if (frame.CascadeCount == 0)
			return;
		commandList->beginMarker("Shadows");
		for (uint32_t c = 0; c < frame.CascadeCount; c++)
		{
			nvrhi::IFramebuffer* framebuffer = m_ShadowFramebuffers[c];
			commandList->clearDepthStencilTexture(m_ShadowMap, nvrhi::TextureSubresourceSet(0, 1, c, 1), true, 1.0f, false, 0);
			const Frustum cascadeFrustum(frame.ShadowMatrices[c]);

			nvrhi::GraphicsState state;
			state.framebuffer = framebuffer;
			state.pipeline = m_ShadowPipeline;
			state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(m_ShadowMapSize), static_cast<float>(m_ShadowMapSize)));
			for (const DrawItem& item : frame.ShadowCasters)
			{
				const Submesh& submesh = item.MeshPtr->Submeshes[item.SubmeshIndex];
				// The cascade frustum test ignores the near plane implicitly via depth clamping of casters.
				const AABB bounds = submesh.Bounds.Transformed(item.Transform);
				if (!cascadeFrustum.Intersects(bounds))
					continue;
				state.bindings = { m_ShadowSet, item.Material->BindingSet };
				state.vertexBuffers = { nvrhi::VertexBufferBinding().setBuffer(item.MeshPtr->VertexBuffer).setSlot(0).setOffset(0) };
				state.indexBuffer = nvrhi::IndexBufferBinding().setBuffer(item.MeshPtr->IndexBuffer).setFormat(nvrhi::Format::R32_UINT).setOffset(0);
				commandList->setGraphicsState(state);
				ShadowConstants constants{ item.Transform, frame.ShadowMatrices[c] };
				commandList->setPushConstants(&constants, sizeof(constants));
				commandList->drawIndexed(nvrhi::DrawArguments().setVertexCount(submesh.IndexCount).setStartIndexLocation(submesh.FirstIndex));
				m_Stats.ShadowDrawCalls++;
			}
		}
		commandList->endMarker();
	}

	void SceneRenderer::RenderPrepass(nvrhi::ICommandList* commandList, const FrameData& frame)
	{
		commandList->beginMarker("Prepass");
		commandList->clearDepthStencilTexture(m_DepthTarget, nvrhi::AllSubresources, true, 1.0f, false, 0);
		commandList->clearTextureFloat(m_NormalTarget, nvrhi::AllSubresources, nvrhi::Color(0.5f, 0.5f, 1.0f, 0.0f));
		commandList->clearTextureUInt(m_EntityIDTarget, nvrhi::AllSubresources, 0u);
		DrawItems(commandList, frame.Opaque, m_PrepassFramebuffer, m_PrepassPipeline, m_PrepassPipelineNoCull, m_FrameSet);
		commandList->endMarker();
	}

	void SceneRenderer::RenderSSAO(nvrhi::ICommandList* commandList)
	{
		if (!m_Settings.SSAOEnabled)
			return;
		commandList->beginMarker("SSAO");
		nvrhi::GraphicsState state;
		state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(m_Width), static_cast<float>(m_Height)));

		state.pipeline = m_SSAOPipeline;
		state.framebuffer = m_SSAOFramebuffer;
		state.bindings = { m_SSAOSet };
		commandList->setGraphicsState(state);
		const SSAOConstants ssao{ m_Settings.SSAORadius, m_Settings.SSAOBias, m_Settings.SSAOPower, m_Settings.SSAOSampleCount };
		commandList->setPushConstants(&ssao, sizeof(ssao));
		commandList->draw(nvrhi::DrawArguments().setVertexCount(3));

		// Depth sharpness is in depth-buffer units (non-linear); a high value keeps edges crisp.
		state.pipeline = m_BlurPipeline;
		state.framebuffer = m_SSAOBlurFramebuffer;
		state.bindings = { m_BlurHorizontalSet };
		commandList->setGraphicsState(state);
		BlurConstants blur{ glm::vec2(1.0f / static_cast<float>(m_Width), 0.0f), 2000.0f, 0.0f };
		commandList->setPushConstants(&blur, sizeof(blur));
		commandList->draw(nvrhi::DrawArguments().setVertexCount(3));

		state.framebuffer = m_SSAOFramebuffer;
		state.bindings = { m_BlurVerticalSet };
		commandList->setGraphicsState(state);
		blur.TexelStep = glm::vec2(0.0f, 1.0f / static_cast<float>(m_Height));
		commandList->setPushConstants(&blur, sizeof(blur));
		commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
		commandList->endMarker();
	}

	void SceneRenderer::RenderForward(nvrhi::ICommandList* commandList, const FrameData& frame)
	{
		commandList->beginMarker("Forward");
		const glm::vec3 background = m_Settings.BackgroundColor;
		commandList->clearTextureFloat(m_HDRTarget, nvrhi::AllSubresources, nvrhi::Color(background.r, background.g, background.b, 1.0f));
		DrawItems(commandList, frame.Opaque, m_HDRFramebuffer, m_OpaquePipeline, m_OpaquePipelineNoCull, m_FrameSet);

		if (frame.DrawSkybox)
		{
			nvrhi::GraphicsState state;
			state.pipeline = m_SkyboxPipeline;
			state.framebuffer = m_HDRFramebuffer;
			state.bindings = { m_FrameSet };
			state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(m_Width), static_cast<float>(m_Height)));
			commandList->setGraphicsState(state);
			const DrawConstants unused{};
			commandList->setPushConstants(&unused, sizeof(unused));
			commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
		}

		DrawItems(commandList, frame.Transparent, m_HDRFramebuffer, m_TransparentPipeline, m_TransparentPipelineNoCull, m_FrameSet);
		commandList->endMarker();
	}

	void SceneRenderer::RenderTonemap(nvrhi::ICommandList* commandList)
	{
		commandList->beginMarker("Tonemap");
		nvrhi::GraphicsState state;
		state.pipeline = m_TonemapPipeline;
		state.framebuffer = m_LDRFramebuffer;
		state.bindings = { m_TonemapSet };
		state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(m_Width), static_cast<float>(m_Height)));
		commandList->setGraphicsState(state);
		const TonemapConstants constants{ std::max(m_Settings.Exposure, 0.0f), static_cast<int32_t>(m_Settings.Tonemapper), 2.2f, 0.0f };
		commandList->setPushConstants(&constants, sizeof(constants));
		commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
		commandList->endMarker();
	}

	void SceneRenderer::Render(Scene& scene, const RenderCamera& camera)
	{
		m_Stats = RenderStats{};
		CreateShadowTargets(); // picks up shadow setting changes

		FrameData frame;
		CollectScene(scene, camera, frame);
		UpdateFrameBindings(frame.EnvironmentMaps);
		m_CurrentEnvironment = frame.EnvironmentMaps;
		m_Stats.UIElements = m_UIRenderer->Prepare(scene, m_Width, m_Height);

		m_CommandList->open();
		m_CommandList->writeBuffer(m_FrameConstants, &frame.Constants, sizeof(frame.Constants));
		RenderShadows(m_CommandList, frame);
		RenderPrepass(m_CommandList, frame);
		RenderSSAO(m_CommandList);
		RenderForward(m_CommandList, frame);
		RenderTonemap(m_CommandList);
		m_UIRenderer->Render(m_CommandList, m_LDRFramebuffer);
		if (m_OverlayCallback)
			m_OverlayCallback(m_CommandList, m_LDRFramebuffer, camera);
		m_CommandList->close();
		m_Device->executeCommandList(m_CommandList);
		m_Assets.EndFrame();
	}

	std::optional<Image> SceneRenderer::CaptureImage()
	{
		return ReadbackTexture(m_Device, m_LDRTarget);
	}

	UUID SceneRenderer::PickEntity(uint32_t x, uint32_t y)
	{
		if (x >= m_Width || y >= m_Height || !m_EntityIDTarget)
			return 0;

		nvrhi::TextureDesc stagingDesc;
		stagingDesc.width = 1;
		stagingDesc.height = 1;
		stagingDesc.format = nvrhi::Format::R32_UINT;
		stagingDesc.debugName = "PickStaging";
		nvrhi::StagingTextureHandle staging = m_Device->createStagingTexture(stagingDesc, nvrhi::CpuAccessMode::Read);

		nvrhi::CommandListHandle commandList = m_Device->createCommandList();
		commandList->open();
		commandList->copyTexture(staging, nvrhi::TextureSlice().setWidth(1).setHeight(1), m_EntityIDTarget,
			nvrhi::TextureSlice().setOrigin(x, y).setWidth(1).setHeight(1));
		commandList->close();
		m_Device->executeCommandList(commandList);
		m_Device->waitForIdle();

		size_t rowPitch = 0;
		const void* mapped = m_Device->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch);
		if (!mapped)
			return 0;
		uint32_t id = 0;
		std::memcpy(&id, mapped, sizeof(id));
		m_Device->unmapStagingTexture(staging);

		if (id == 0 || id > m_PickTable.size())
			return 0;
		return m_PickTable[id - 1];
	}

}
