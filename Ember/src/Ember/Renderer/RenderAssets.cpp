#include "Ember/Renderer/RenderAssets.h"

#include "Ember/Core/FileSystem.h"
#include "Ember/Core/Log.h"
#include "Ember/Renderer/MaterialSerializer.h"
#include "Ember/Renderer/Texture.h"

#include <cgltf.h>
#include <nvrhi/utils.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>

namespace Ember {

	namespace {

		template<typename T>
		void HashCombine(size_t& seed, const T& value)
		{
			seed ^= std::hash<T>()(value) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
		}

		struct MaterialConstants
		{
			glm::vec4 BaseColor;
			glm::vec4 Emissive;
			glm::vec4 Params;
			glm::vec4 AlphaParams;
			glm::uvec4 TextureFlags;
			glm::uvec4 TextureFlags2;
		};
		static_assert(sizeof(MaterialConstants) == 96);

		struct ProceduralSkyConstants
		{
			glm::vec4 Zenith;
			glm::vec4 Horizon;
			glm::vec4 Ground;
			glm::vec4 SunDirection;
			float Size;
			float Padding[3];
		};

		struct CubeConstants
		{
			float Size;
			float SourceLevel;
			float Extra0;
			float Extra1;
			float Extra2;
			float Padding[3];
		};

		std::string Lowercase(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

	}

	size_t HashMaterialParameters(const MaterialParameters& m)
	{
		size_t seed = 0;
		for (int i = 0; i < 4; i++)
			HashCombine(seed, m.BaseColor[i]);
		HashCombine(seed, m.Metallic);
		HashCombine(seed, m.Roughness);
		for (int i = 0; i < 3; i++)
			HashCombine(seed, m.EmissiveColor[i]);
		HashCombine(seed, m.EmissiveStrength);
		HashCombine(seed, m.NormalScale);
		HashCombine(seed, m.OcclusionStrength);
		HashCombine(seed, static_cast<int>(m.Alpha));
		HashCombine(seed, m.AlphaCutoff);
		HashCombine(seed, m.DoubleSided);
		HashCombine(seed, m.BaseColorTexture);
		HashCombine(seed, m.MetallicRoughnessTexture);
		HashCombine(seed, m.NormalTexture);
		HashCombine(seed, m.OcclusionTexture);
		HashCombine(seed, m.EmissiveTexture);
		return seed;
	}

	nvrhi::BindingLayoutDesc RenderAssets::MakeLayoutDesc(nvrhi::ShaderType visibility, uint32_t registerSpace)
	{
		nvrhi::BindingLayoutDesc desc;
		desc.visibility = visibility;
		desc.registerSpace = registerSpace;
		desc.registerSpaceIsDescriptorSet = true;
		desc.bindingOffsets.setShaderResourceOffset(0).setSamplerOffset(0).setConstantBufferOffset(0).setUnorderedAccessViewOffset(0);
		return desc;
	}

	RenderAssets::RenderAssets(nvrhi::IDevice* device, const RenderSettings& settings)
		: m_Device(device), m_Settings(settings), m_Shaders(device)
	{
		// Samplers
		auto samplerDesc = nvrhi::SamplerDesc().setAllAddressModes(nvrhi::SamplerAddressMode::Wrap).setMaxAnisotropy(8.0f);
		m_LinearWrap = device->createSampler(samplerDesc);
		m_LinearClamp = device->createSampler(nvrhi::SamplerDesc().setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
		m_PointClamp = device->createSampler(nvrhi::SamplerDesc().setAllAddressModes(nvrhi::SamplerAddressMode::Clamp).setAllFilters(false));
		m_ShadowCompare = device->createSampler(nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Border).setBorderColor(nvrhi::Color(1.0f))
			.setReductionType(nvrhi::SamplerReductionType::Comparison));

		// Binding layouts
		{
			nvrhi::BindingLayoutDesc desc = MakeLayoutDesc(nvrhi::ShaderType::All);
			desc.bindings = {
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
				nvrhi::BindingLayoutItem::Texture_SRV(1),
				nvrhi::BindingLayoutItem::Texture_SRV(2),
				nvrhi::BindingLayoutItem::Texture_SRV(3),
				nvrhi::BindingLayoutItem::Texture_SRV(4),
				nvrhi::BindingLayoutItem::Texture_SRV(5),
				nvrhi::BindingLayoutItem::Sampler(6),
				nvrhi::BindingLayoutItem::Sampler(7),
				nvrhi::BindingLayoutItem::Texture_SRV(8),
				nvrhi::BindingLayoutItem::Sampler(9),
				nvrhi::BindingLayoutItem::PushConstants(c_PushConstantSlot, 120),
			};
			m_FrameLayout = device->createBindingLayout(desc);
		}
		{
			nvrhi::BindingLayoutDesc desc = MakeLayoutDesc(nvrhi::ShaderType::All, 1);
			desc.bindings = {
				nvrhi::BindingLayoutItem::ConstantBuffer(0),
				nvrhi::BindingLayoutItem::Texture_SRV(1),
				nvrhi::BindingLayoutItem::Texture_SRV(2),
				nvrhi::BindingLayoutItem::Texture_SRV(3),
				nvrhi::BindingLayoutItem::Texture_SRV(4),
				nvrhi::BindingLayoutItem::Texture_SRV(5),
				nvrhi::BindingLayoutItem::Sampler(6),
			};
			m_MaterialLayout = device->createBindingLayout(desc);
		}
		{
			nvrhi::BindingLayoutDesc desc = MakeLayoutDesc(nvrhi::ShaderType::All);
			desc.bindings = { nvrhi::BindingLayoutItem::PushConstants(c_PushConstantSlot, 128) };
			m_ShadowLayout = device->createBindingLayout(desc);
		}
		{
			nvrhi::BindingLayoutDesc desc = MakeLayoutDesc(nvrhi::ShaderType::Compute);
			desc.bindings = {
				nvrhi::BindingLayoutItem::Texture_SRV(0),
				nvrhi::BindingLayoutItem::Sampler(1),
				nvrhi::BindingLayoutItem::Texture_UAV(2),
				nvrhi::BindingLayoutItem::PushConstants(c_PushConstantSlot, sizeof(ProceduralSkyConstants)),
			};
			m_ComputeLayout = device->createBindingLayout(desc);
		}

		// Vertex input layout (locations follow attribute order).
		{
			const nvrhi::VertexAttributeDesc attributes[] = {
				nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RGB32_FLOAT)
					.setOffset(offsetof(Vertex, Position)).setElementStride(sizeof(Vertex)),
				nvrhi::VertexAttributeDesc().setName("NORMAL").setFormat(nvrhi::Format::RGB32_FLOAT)
					.setOffset(offsetof(Vertex, Normal)).setElementStride(sizeof(Vertex)),
				nvrhi::VertexAttributeDesc().setName("TANGENT").setFormat(nvrhi::Format::RGBA32_FLOAT)
					.setOffset(offsetof(Vertex, Tangent)).setElementStride(sizeof(Vertex)),
				nvrhi::VertexAttributeDesc().setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT)
					.setOffset(offsetof(Vertex, TexCoord)).setElementStride(sizeof(Vertex)),
			};
			nvrhi::ShaderHandle vs = m_Shaders.Get("Mesh", nvrhi::ShaderType::Vertex);
			m_MeshInputLayout = device->createInputLayout(attributes, 4, vs);

			// Shadow pass consumes only position (location 0) and texcoord (location 1).
			const nvrhi::VertexAttributeDesc shadowAttributes[] = { attributes[0], attributes[3] };
			m_ShadowInputLayout = device->createInputLayout(shadowAttributes, 2, m_Shaders.Get("Shadow", nvrhi::ShaderType::Vertex));
		}

		// Default textures
		nvrhi::CommandListHandle commandList = device->createCommandList();
		commandList->open();
		m_WhiteTexture = TextureUtils::CreateSolidTexture(device, commandList, 255, 255, 255, 255, true, "White");
		m_WhiteLinearTexture = TextureUtils::CreateSolidTexture(device, commandList, 255, 255, 255, 255, false, "WhiteLinear");
		m_BlackTexture = TextureUtils::CreateSolidTexture(device, commandList, 0, 0, 0, 255, false, "Black");
		m_FlatNormalTexture = TextureUtils::CreateSolidTexture(device, commandList, 128, 128, 255, 255, false, "FlatNormal");
		commandList->close();
		device->executeCommandList(commandList);

		CreateBRDFLut();
	}

	RenderAssets::~RenderAssets()
	{
		m_Device->waitForIdle();
	}

	void RenderAssets::SetAssetDirectory(const std::filesystem::path& directory)
	{
		if (m_AssetDirectory != directory)
		{
			m_AssetDirectory = directory;
			ClearCaches();
		}
	}

	void RenderAssets::ClearCaches()
	{
		m_Device->waitForIdle();
		m_Models.clear();
		m_Textures.clear();
		m_EmbeddedImages.clear();
		m_MaterialAssets.clear();
		m_Environments.clear();
		m_Materials.clear();
		m_FailedAssets.clear();
	}

	void RenderAssets::EndFrame()
	{
		m_FrameIndex++;
		constexpr uint64_t evictAfterFrames = 600;
		if (m_FrameIndex % 120 != 0)
			return;
		for (auto it = m_Materials.begin(); it != m_Materials.end();)
		{
			if (m_FrameIndex - it->second->LastUsedFrame > evictAfterFrames)
				it = m_Materials.erase(it);
			else
				++it;
		}
	}

	void RenderAssets::SubmitAndWait(nvrhi::ICommandList* commandList)
	{
		commandList->close();
		m_Device->executeCommandList(commandList);
		m_Device->waitForIdle();
	}

	// ----------------------------------------------------------------------------------------------
	// Models

	Ref<Model> RenderAssets::GetModel(const std::string& path)
	{
		if (path.empty())
			return nullptr;
		if (auto it = m_Models.find(path); it != m_Models.end())
			return it->second;
		if (m_FailedAssets.contains(path))
			return nullptr;

		Ref<Model> model;
		const std::string prefix = c_BuiltinPrefix;
		if (path.rfind(prefix, 0) == 0)
		{
			MeshData data = MeshFactory::CreateByName(path.substr(prefix.size()));
			if (data.IsValid())
			{
				nvrhi::CommandListHandle commandList = m_Device->createCommandList();
				commandList->open();
				model = CreateRef<Model>();
				model->Path = path;
				model->MeshData = Mesh::Create(m_Device, commandList, data, path);
				model->Materials = { MaterialParameters{} };
				commandList->close();
				m_Device->executeCommandList(commandList);
			}
			else
			{
				EMBER_CORE_ERROR("Unknown built-in mesh '{}' (available: Cube, Plane, Sphere, Cylinder, Capsule)", path);
			}
		}
		else
		{
			model = LoadModelFromFile(path);
		}

		if (!model || !model->MeshData)
		{
			m_FailedAssets.insert(path);
			return nullptr;
		}
		m_Models[path] = model;
		return model;
	}

	Ref<Model> RenderAssets::LoadModelFromFile(const std::string& path)
	{
		if (!FileSystem::IsSafeRelativePath(path))
		{
			EMBER_CORE_ERROR("Model path '{}' must be relative to the asset directory", path);
			return nullptr;
		}
		const std::string extension = Lowercase(std::filesystem::path(path).extension().string());
		if (extension == ".gltf" || extension == ".glb")
			return LoadGltf(path);
		EMBER_CORE_ERROR("Unsupported model format '{}' (use .gltf or .glb)", path);
		return nullptr;
	}

	Ref<Model> RenderAssets::LoadGltf(const std::string& path)
	{
		const std::filesystem::path fullPath = m_AssetDirectory / path;
		const std::string fullPathString = fullPath.string();

		cgltf_options options{};
		cgltf_data* data = nullptr;
		cgltf_result result = cgltf_parse_file(&options, fullPathString.c_str(), &data);
		if (result != cgltf_result_success)
		{
			EMBER_CORE_ERROR("Failed to parse glTF '{}' (error {})", path, static_cast<int>(result));
			return nullptr;
		}
		std::unique_ptr<cgltf_data, decltype(&cgltf_free)> guard(data, &cgltf_free);

		result = cgltf_load_buffers(&options, data, fullPathString.c_str());
		if (result != cgltf_result_success)
		{
			EMBER_CORE_ERROR("Failed to load glTF buffers for '{}' (error {})", path, static_cast<int>(result));
			return nullptr;
		}
		result = cgltf_validate(data);
		if (result != cgltf_result_success)
		{
			EMBER_CORE_ERROR("glTF '{}' failed validation (error {})", path, static_cast<int>(result));
			return nullptr;
		}

		auto model = CreateRef<Model>();
		model->Path = path;

		// --- Images: external files become asset-relative paths, embedded images are kept encoded in the
		// texture cache's source map under "<path>#<index>".
		const std::filesystem::path modelDirectory = std::filesystem::path(path).parent_path();
		std::vector<std::string> imagePaths(data->images_count);
		for (cgltf_size i = 0; i < data->images_count; i++)
		{
			const cgltf_image& image = data->images[i];
			const std::string key = path + "#" + std::to_string(i);
			if (image.buffer_view)
			{
				const uint8_t* bytes = static_cast<const uint8_t*>(cgltf_buffer_view_data(image.buffer_view));
				if (bytes)
				{
					m_EmbeddedImages[key].assign(bytes, bytes + image.buffer_view->size);
					imagePaths[i] = key;
				}
			}
			else if (image.uri && std::strncmp(image.uri, "data:", 5) == 0)
			{
				const char* comma = std::strchr(image.uri, ',');
				if (comma && std::strstr(image.uri, ";base64,"))
				{
					const char* base64 = comma + 1;
					const size_t length = std::strlen(base64);
					size_t padding = 0;
					if (length >= 1 && base64[length - 1] == '=') padding++;
					if (length >= 2 && base64[length - 2] == '=') padding++;
					const size_t decodedSize = length / 4 * 3 - padding;
					void* decoded = nullptr;
					if (cgltf_load_buffer_base64(&options, decodedSize, base64, &decoded) == cgltf_result_success)
					{
						const auto* bytes = static_cast<const uint8_t*>(decoded);
						m_EmbeddedImages[key].assign(bytes, bytes + decodedSize);
						imagePaths[i] = key;
						std::free(decoded);
					}
				}
			}
			else if (image.uri)
			{
				std::string uri = image.uri;
				std::vector<char> buffer(uri.begin(), uri.end());
				buffer.push_back('\0');
				cgltf_decode_uri(buffer.data());
				imagePaths[i] = (modelDirectory / std::filesystem::path(buffer.data())).generic_string();
			}
		}

		auto texturePath = [&](const cgltf_texture_view& view) -> std::string
		{
			if (!view.texture || !view.texture->image)
				return {};
			return imagePaths[static_cast<size_t>(view.texture->image - data->images)];
		};

		// --- Materials
		for (cgltf_size i = 0; i < data->materials_count; i++)
		{
			const cgltf_material& source = data->materials[i];
			MaterialParameters material;
			if (source.has_pbr_metallic_roughness)
			{
				const cgltf_pbr_metallic_roughness& pbr = source.pbr_metallic_roughness;
				material.BaseColor = glm::vec4(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3]);
				material.Metallic = pbr.metallic_factor;
				material.Roughness = pbr.roughness_factor;
				material.BaseColorTexture = texturePath(pbr.base_color_texture);
				material.MetallicRoughnessTexture = texturePath(pbr.metallic_roughness_texture);
			}
			material.NormalTexture = texturePath(source.normal_texture);
			material.NormalScale = source.normal_texture.texture ? source.normal_texture.scale : 1.0f;
			material.OcclusionTexture = texturePath(source.occlusion_texture);
			material.OcclusionStrength = source.occlusion_texture.texture ? source.occlusion_texture.scale : 1.0f;
			material.EmissiveTexture = texturePath(source.emissive_texture);
			material.EmissiveColor = glm::vec3(source.emissive_factor[0], source.emissive_factor[1], source.emissive_factor[2]);
			material.EmissiveStrength = source.has_emissive_strength ? source.emissive_strength.emissive_strength : 1.0f;
			material.Alpha = source.alpha_mode == cgltf_alpha_mode_mask ? AlphaMode::Mask
				: source.alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::Blend : AlphaMode::Opaque;
			material.AlphaCutoff = source.alpha_cutoff;
			material.DoubleSided = source.double_sided;
			model->Materials.push_back(material);
		}
		const uint32_t defaultMaterialIndex = static_cast<uint32_t>(model->Materials.size());
		bool usesDefaultMaterial = false;

		// --- Geometry: flatten the node hierarchy into one mesh (node transforms baked into vertices).
		MeshData meshData;
		auto appendPrimitive = [&](const cgltf_primitive& primitive, const glm::mat4& transform)
		{
			if (primitive.type != cgltf_primitive_type_triangles)
				return;
			const cgltf_accessor* positions = nullptr;
			const cgltf_accessor* normals = nullptr;
			const cgltf_accessor* tangents = nullptr;
			const cgltf_accessor* texcoords = nullptr;
			for (cgltf_size a = 0; a < primitive.attributes_count; a++)
			{
				const cgltf_attribute& attribute = primitive.attributes[a];
				switch (attribute.type)
				{
					case cgltf_attribute_type_position: positions = attribute.data; break;
					case cgltf_attribute_type_normal:   normals = attribute.data; break;
					case cgltf_attribute_type_tangent:  tangents = attribute.data; break;
					case cgltf_attribute_type_texcoord: if (attribute.index == 0) texcoords = attribute.data; break;
					default: break;
				}
			}
			if (!positions || positions->count == 0)
				return;

			MeshData part;
			part.Vertices.resize(positions->count);
			for (cgltf_size v = 0; v < positions->count; v++)
			{
				Vertex& vertex = part.Vertices[v];
				cgltf_accessor_read_float(positions, v, &vertex.Position.x, 3);
				if (normals)
					cgltf_accessor_read_float(normals, v, &vertex.Normal.x, 3);
				if (tangents)
					cgltf_accessor_read_float(tangents, v, &vertex.Tangent.x, 4);
				if (texcoords)
					cgltf_accessor_read_float(texcoords, v, &vertex.TexCoord.x, 2);
			}
			if (primitive.indices)
			{
				part.Indices.resize(primitive.indices->count);
				for (cgltf_size i = 0; i < primitive.indices->count; i++)
					part.Indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i));
			}
			else
			{
				part.Indices.resize(positions->count);
				for (cgltf_size i = 0; i < positions->count; i++)
					part.Indices[i] = static_cast<uint32_t>(i);
			}
			part.Indices.resize(part.Indices.size() / 3 * 3);
			for (uint32_t index : part.Indices)
			{
				if (index >= part.Vertices.size())
				{
					EMBER_CORE_WARN("glTF '{}': primitive with out-of-range indices skipped", path);
					return;
				}
			}
			if (!normals)
				part.GenerateNormals();
			if (!tangents)
				part.GenerateTangents();

			// Bake the node transform.
			const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));
			const bool mirrored = glm::determinant(glm::mat3(transform)) < 0.0f;
			for (Vertex& vertex : part.Vertices)
			{
				vertex.Position = glm::vec3(transform * glm::vec4(vertex.Position, 1.0f));
				vertex.Normal = glm::normalize(normalMatrix * vertex.Normal);
				const glm::vec3 tangent = glm::mat3(transform) * glm::vec3(vertex.Tangent);
				vertex.Tangent = glm::vec4(glm::length(tangent) > 0.0f ? glm::normalize(tangent) : glm::vec3(1, 0, 0),
					mirrored ? -vertex.Tangent.w : vertex.Tangent.w);
			}
			if (mirrored)
			{
				for (size_t i = 0; i + 2 < part.Indices.size(); i += 3)
					std::swap(part.Indices[i + 1], part.Indices[i + 2]);
			}

			uint32_t materialIndex = defaultMaterialIndex;
			if (primitive.material)
				materialIndex = static_cast<uint32_t>(primitive.material - data->materials);
			else
				usesDefaultMaterial = true;

			const uint32_t baseVertex = static_cast<uint32_t>(meshData.Vertices.size());
			Submesh submesh;
			submesh.FirstIndex = static_cast<uint32_t>(meshData.Indices.size());
			submesh.IndexCount = static_cast<uint32_t>(part.Indices.size());
			submesh.MaterialIndex = materialIndex;
			meshData.Vertices.insert(meshData.Vertices.end(), part.Vertices.begin(), part.Vertices.end());
			for (uint32_t index : part.Indices)
				meshData.Indices.push_back(index + baseVertex);
			meshData.Submeshes.push_back(submesh);
		};

		std::function<void(const cgltf_node*)> visit = [&](const cgltf_node* node)
		{
			if (node->mesh)
			{
				float world[16];
				cgltf_node_transform_world(node, world);
				glm::mat4 transform;
				std::memcpy(&transform[0][0], world, sizeof(world));
				for (cgltf_size p = 0; p < node->mesh->primitives_count; p++)
					appendPrimitive(node->mesh->primitives[p], transform);
			}
			for (cgltf_size c = 0; c < node->children_count; c++)
				visit(node->children[c]);
		};

		const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
		if (scene)
		{
			for (cgltf_size n = 0; n < scene->nodes_count; n++)
				visit(scene->nodes[n]);
		}
		else
		{
			for (cgltf_size m = 0; m < data->meshes_count; m++)
			{
				for (cgltf_size p = 0; p < data->meshes[m].primitives_count; p++)
					appendPrimitive(data->meshes[m].primitives[p], glm::mat4(1.0f));
			}
		}

		if (usesDefaultMaterial)
			model->Materials.push_back(MaterialParameters{});

		if (!meshData.IsValid())
		{
			EMBER_CORE_ERROR("glTF '{}' contains no triangle geometry", path);
			return nullptr;
		}

		nvrhi::CommandListHandle commandList = m_Device->createCommandList();
		commandList->open();
		model->MeshData = Mesh::Create(m_Device, commandList, meshData, path);
		commandList->close();
		m_Device->executeCommandList(commandList);
		if (!model->MeshData)
			return nullptr;

		EMBER_CORE_INFO("Loaded model '{}': {} vertices, {} triangles, {} submeshes, {} materials", path,
			meshData.Vertices.size(), meshData.Indices.size() / 3, meshData.Submeshes.size(), model->Materials.size());
		return model;
	}

	// ----------------------------------------------------------------------------------------------
	// Textures and materials

	nvrhi::ITexture* RenderAssets::GetTexture(const std::string& path, bool srgb)
	{
		if (path.empty())
			return nullptr;
		const std::string key = path + (srgb ? "|srgb" : "|linear");
		if (auto it = m_Textures.find(key); it != m_Textures.end())
			return it->second;
		if (m_FailedAssets.contains(key))
			return nullptr;

		std::optional<Image> image;
		if (auto embedded = m_EmbeddedImages.find(path); embedded != m_EmbeddedImages.end())
		{
			image = TextureUtils::DecodeImage(embedded->second.data(), embedded->second.size());
		}
		else if (FileSystem::IsSafeRelativePath(path))
		{
			if (std::optional<Buffer> bytes = FileSystem::ReadBinary(m_AssetDirectory / path))
				image = TextureUtils::DecodeImage(bytes->Data(), bytes->Size());
		}

		if (!image)
		{
			EMBER_CORE_ERROR("Failed to load texture '{}'", path);
			m_FailedAssets.insert(key);
			return nullptr;
		}

		nvrhi::CommandListHandle commandList = m_Device->createCommandList();
		commandList->open();
		nvrhi::TextureHandle texture = TextureUtils::CreateTexture2D(m_Device, commandList, *image, srgb, true, path);
		commandList->close();
		m_Device->executeCommandList(commandList);
		if (!texture)
		{
			m_FailedAssets.insert(key);
			return nullptr;
		}
		m_Textures[key] = texture;
		return texture;
	}

	std::optional<MaterialParameters> RenderAssets::GetMaterialAsset(const std::string& path)
	{
		if (path.empty())
			return std::nullopt;
		if (auto it = m_MaterialAssets.find(path); it != m_MaterialAssets.end())
			return it->second;
		if (m_FailedAssets.contains(path) || !FileSystem::IsSafeRelativePath(path))
			return std::nullopt;
		std::optional<MaterialParameters> material = MaterialSerializer::Load(m_AssetDirectory / path);
		if (!material)
		{
			m_FailedAssets.insert(path);
			return std::nullopt;
		}
		m_MaterialAssets[path] = *material;
		return material;
	}

	GpuMaterial* RenderAssets::GetMaterial(const MaterialParameters& parameters)
	{
		const size_t hash = HashMaterialParameters(parameters);
		auto range = m_Materials.equal_range(hash);
		for (auto it = range.first; it != range.second; ++it)
		{
			if (it->second->Parameters == parameters)
			{
				it->second->LastUsedFrame = m_FrameIndex;
				return it->second.get();
			}
		}

		auto material = CreateScope<GpuMaterial>();
		material->Parameters = parameters;
		material->LastUsedFrame = m_FrameIndex;

		nvrhi::ITexture* baseColor = GetTexture(parameters.BaseColorTexture, true);
		nvrhi::ITexture* metallicRoughness = GetTexture(parameters.MetallicRoughnessTexture, false);
		nvrhi::ITexture* normal = GetTexture(parameters.NormalTexture, false);
		nvrhi::ITexture* occlusion = GetTexture(parameters.OcclusionTexture, false);
		nvrhi::ITexture* emissive = GetTexture(parameters.EmissiveTexture, true);

		MaterialConstants constants{};
		constants.BaseColor = parameters.BaseColor;
		constants.Emissive = glm::vec4(parameters.EmissiveColor * parameters.EmissiveStrength, 0.0f);
		constants.Params = glm::vec4(parameters.Metallic, parameters.Roughness, parameters.NormalScale, parameters.OcclusionStrength);
		constants.AlphaParams = glm::vec4(static_cast<float>(parameters.Alpha), parameters.AlphaCutoff, 0.0f, 0.0f);
		constants.TextureFlags = glm::uvec4(baseColor ? 1u : 0u, metallicRoughness ? 1u : 0u, normal ? 1u : 0u, occlusion ? 1u : 0u);
		constants.TextureFlags2 = glm::uvec4(emissive ? 1u : 0u, 0u, 0u, 0u);

		nvrhi::BufferDesc bufferDesc;
		bufferDesc.byteSize = sizeof(MaterialConstants);
		bufferDesc.isConstantBuffer = true;
		bufferDesc.debugName = "MaterialConstants";
		bufferDesc.initialState = nvrhi::ResourceStates::ConstantBuffer;
		bufferDesc.keepInitialState = true;
		material->Constants = m_Device->createBuffer(bufferDesc);

		nvrhi::CommandListHandle commandList = m_Device->createCommandList();
		commandList->open();
		commandList->writeBuffer(material->Constants, &constants, sizeof(constants));
		commandList->close();
		m_Device->executeCommandList(commandList);

		nvrhi::BindingSetDesc setDesc;
		setDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, material->Constants),
			nvrhi::BindingSetItem::Texture_SRV(1, baseColor ? baseColor : m_WhiteTexture.Get()),
			nvrhi::BindingSetItem::Texture_SRV(2, metallicRoughness ? metallicRoughness : m_WhiteLinearTexture.Get()),
			nvrhi::BindingSetItem::Texture_SRV(3, normal ? normal : m_FlatNormalTexture.Get()),
			nvrhi::BindingSetItem::Texture_SRV(4, occlusion ? occlusion : m_WhiteLinearTexture.Get()),
			nvrhi::BindingSetItem::Texture_SRV(5, emissive ? emissive : m_WhiteTexture.Get()),
			nvrhi::BindingSetItem::Sampler(6, m_LinearWrap),
		};
		material->BindingSet = m_Device->createBindingSet(setDesc, m_MaterialLayout);

		GpuMaterial* result = material.get();
		m_Materials.emplace(hash, std::move(material));
		return result;
	}

	// ----------------------------------------------------------------------------------------------
	// Image-based lighting

	nvrhi::ComputePipelineHandle RenderAssets::GetComputePipeline(const char* shader)
	{
		if (auto it = m_ComputePipelines.find(shader); it != m_ComputePipelines.end())
			return it->second;
		nvrhi::ComputePipelineDesc desc;
		desc.CS = m_Shaders.Get(shader, nvrhi::ShaderType::Compute);
		desc.bindingLayouts = { m_ComputeLayout };
		nvrhi::ComputePipelineHandle pipeline = desc.CS ? m_Device->createComputePipeline(desc) : nullptr;
		m_ComputePipelines[shader] = pipeline;
		return pipeline;
	}

	void RenderAssets::RunCompute(nvrhi::ICommandList* commandList, const char* shader, nvrhi::ITexture* input,
		nvrhi::TextureDimension inputDimension, nvrhi::ITexture* output, uint32_t outputMip, bool outputIsCube,
		const void* constants, uint32_t constantsSize, uint32_t width, uint32_t height, uint32_t layers)
	{
		nvrhi::ComputePipelineHandle pipeline = GetComputePipeline(shader);
		if (!pipeline)
			return;

		nvrhi::BindingSetDesc setDesc;
		setDesc.bindings = {
			nvrhi::BindingSetItem::Texture_SRV(0, input ? input : m_WhiteTexture.Get(), nvrhi::Format::UNKNOWN, nvrhi::AllSubresources,
				input ? inputDimension : nvrhi::TextureDimension::Texture2D),
			nvrhi::BindingSetItem::Sampler(1, m_LinearClamp),
			nvrhi::BindingSetItem::Texture_UAV(2, output, nvrhi::Format::UNKNOWN,
				nvrhi::TextureSubresourceSet(outputMip, 1, 0, outputIsCube ? 6 : 1),
				outputIsCube ? nvrhi::TextureDimension::Texture2DArray : nvrhi::TextureDimension::Texture2D),
			nvrhi::BindingSetItem::PushConstants(c_PushConstantSlot, sizeof(ProceduralSkyConstants)),
		};
		nvrhi::BindingSetHandle set = m_Device->createBindingSet(setDesc, m_ComputeLayout);

		// All compute shaders use a push-constant block no larger than ProceduralSkyConstants.
		uint8_t padded[sizeof(ProceduralSkyConstants)]{};
		std::memcpy(padded, constants, std::min<size_t>(constantsSize, sizeof(padded)));

		nvrhi::ComputeState state;
		state.pipeline = pipeline;
		state.bindings = { set };
		commandList->setComputeState(state);
		commandList->setPushConstants(padded, sizeof(padded));
		commandList->dispatch((width + 7) / 8, (height + 7) / 8, layers);
	}

	void RenderAssets::CreateBRDFLut()
	{
		constexpr uint32_t size = 128;
		nvrhi::TextureDesc desc;
		desc.width = size;
		desc.height = size;
		desc.format = nvrhi::Format::RGBA16_FLOAT;
		desc.isUAV = true;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.keepInitialState = true;
		desc.debugName = "BRDFLut";
		m_BRDFLut = m_Device->createTexture(desc);

		nvrhi::CommandListHandle commandList = m_Device->createCommandList();
		commandList->open();
		const CubeConstants constants{ static_cast<float>(size), 256.0f, 0.0f, 0.0f, 0.0f, {} };
		RunCompute(commandList, "BRDFLut", nullptr, nvrhi::TextureDimension::Texture2D, m_BRDFLut, 0, false,
			&constants, sizeof(constants), size, size, 1);
		SubmitAndWait(commandList);
	}

	Ref<Environment> RenderAssets::GetEnvironment(const std::string& hdrPath)
	{
		if (auto it = m_Environments.find(hdrPath); it != m_Environments.end())
			return it->second;
		if (!hdrPath.empty() && m_FailedAssets.contains(hdrPath))
			return GetEnvironment(std::string());

		Ref<Environment> environment = BuildEnvironment(hdrPath);
		if (!environment)
		{
			m_FailedAssets.insert(hdrPath);
			return hdrPath.empty() ? nullptr : GetEnvironment(std::string());
		}
		m_Environments[hdrPath] = environment;
		return environment;
	}

	Ref<Environment> RenderAssets::BuildEnvironment(const std::string& hdrPath)
	{
		// Load and validate the source before opening a command list: a list that is opened but never
		// submitted would leak its Vulkan command buffer.
		std::optional<HDRImage> image;
		if (!hdrPath.empty())
		{
			if (!FileSystem::IsSafeRelativePath(hdrPath))
			{
				EMBER_CORE_ERROR("Environment map path '{}' must be relative to the asset directory", hdrPath);
				return nullptr;
			}
			image = TextureUtils::LoadHDR(m_AssetDirectory / hdrPath);
			if (!image || !image->IsValid())
				return nullptr;
		}

		nvrhi::CommandListHandle commandList = m_Device->createCommandList();
		commandList->open();
		nvrhi::TextureHandle equirect;
		if (image)
			equirect = TextureUtils::CreateTextureHDR(m_Device, commandList, *image, hdrPath);

		auto makeCube = [&](uint32_t size, uint32_t mips, const char* name)
		{
			nvrhi::TextureDesc desc;
			desc.width = size;
			desc.height = size;
			desc.arraySize = 6;
			desc.mipLevels = mips;
			desc.dimension = nvrhi::TextureDimension::TextureCube;
			desc.format = nvrhi::Format::RGBA16_FLOAT;
			desc.isUAV = true;
			desc.initialState = nvrhi::ResourceStates::ShaderResource;
			desc.keepInitialState = true;
			desc.debugName = name;
			return m_Device->createTexture(desc);
		};

		const uint32_t envSize = m_Settings.EnvironmentCubeSize;
		const uint32_t envMips = TextureUtils::MipCount(envSize, envSize);
		auto environment = CreateRef<Environment>();
		environment->Source = hdrPath;
		environment->EnvironmentCube = makeCube(envSize, envMips, "EnvironmentCube");
		environment->Irradiance = makeCube(m_Settings.IrradianceSize, 1, "Irradiance");
		const uint32_t prefilteredMips = std::min(6u, TextureUtils::MipCount(m_Settings.PrefilteredSize, m_Settings.PrefilteredSize));
		environment->Prefiltered = makeCube(m_Settings.PrefilteredSize, prefilteredMips, "Prefiltered");
		environment->PrefilteredMipCount = prefilteredMips;

		// 1. Base environment cube (mip 0)
		if (equirect)
		{
			const CubeConstants constants{ static_cast<float>(envSize), 0.0f, 0.0f, 0.0f, 0.0f, {} };
			RunCompute(commandList, "EquirectToCube", equirect, nvrhi::TextureDimension::Texture2D, environment->EnvironmentCube, 0, true,
				&constants, sizeof(constants), envSize, envSize, 6);
		}
		else
		{
			ProceduralSkyConstants constants{};
			constants.Zenith = glm::vec4(0.18f, 0.36f, 0.85f, 1.0f);
			constants.Horizon = glm::vec4(0.75f, 0.82f, 0.92f, 1.0f);
			constants.Ground = glm::vec4(0.22f, 0.20f, 0.18f, 1.0f);
			constants.SunDirection = glm::vec4(glm::normalize(glm::vec3(0.4f, 0.7f, 0.3f)), 1.0f);
			constants.Size = static_cast<float>(envSize);
			RunCompute(commandList, "ProceduralSky", nullptr, nvrhi::TextureDimension::Texture2D, environment->EnvironmentCube, 0, true,
				&constants, sizeof(constants), envSize, envSize, 6);
		}

		// 2. Mip chain (each level sampled from the previous one through a single-mip view)
		for (uint32_t mip = 1; mip < envMips; mip++)
		{
			const uint32_t size = std::max(envSize >> mip, 1u);
			nvrhi::ComputePipelineHandle pipeline = GetComputePipeline("CubeDownsample");
			nvrhi::BindingSetDesc setDesc;
			setDesc.bindings = {
				nvrhi::BindingSetItem::Texture_SRV(0, environment->EnvironmentCube, nvrhi::Format::UNKNOWN,
					nvrhi::TextureSubresourceSet(mip - 1, 1, 0, 6), nvrhi::TextureDimension::TextureCube),
				nvrhi::BindingSetItem::Sampler(1, m_LinearClamp),
				nvrhi::BindingSetItem::Texture_UAV(2, environment->EnvironmentCube, nvrhi::Format::UNKNOWN,
					nvrhi::TextureSubresourceSet(mip, 1, 0, 6), nvrhi::TextureDimension::Texture2DArray),
				nvrhi::BindingSetItem::PushConstants(c_PushConstantSlot, sizeof(ProceduralSkyConstants)),
			};
			nvrhi::BindingSetHandle set = m_Device->createBindingSet(setDesc, m_ComputeLayout);
			uint8_t padded[sizeof(ProceduralSkyConstants)]{};
			const CubeConstants constants{ static_cast<float>(size), 0.0f, 0.0f, 0.0f, 0.0f, {} };
			std::memcpy(padded, &constants, sizeof(constants));
			nvrhi::ComputeState state;
			state.pipeline = pipeline;
			state.bindings = { set };
			commandList->setComputeState(state);
			commandList->setPushConstants(padded, sizeof(padded));
			commandList->dispatch((size + 7) / 8, (size + 7) / 8, 6);
		}

		// 3. Diffuse irradiance (sampled from a pre-blurred mip to avoid aliasing)
		{
			const float sourceLevel = std::max(0.0f, std::log2(static_cast<float>(envSize) / 32.0f));
			const CubeConstants constants{ static_cast<float>(m_Settings.IrradianceSize), sourceLevel, 0.05f, 0.0f, 0.0f, {} };
			RunCompute(commandList, "Irradiance", environment->EnvironmentCube, nvrhi::TextureDimension::TextureCube, environment->Irradiance, 0, true,
				&constants, sizeof(constants), m_Settings.IrradianceSize, m_Settings.IrradianceSize, 6);
		}

		// 4. Specular pre-filtering, one roughness per mip
		for (uint32_t mip = 0; mip < prefilteredMips; mip++)
		{
			const uint32_t size = std::max(m_Settings.PrefilteredSize >> mip, 1u);
			const float roughness = prefilteredMips > 1 ? static_cast<float>(mip) / static_cast<float>(prefilteredMips - 1) : 0.0f;
			const CubeConstants constants{ static_cast<float>(size), roughness, static_cast<float>(envSize),
				static_cast<float>(m_Settings.PrefilterSampleCount), static_cast<float>(envMips - 1), {} };
			RunCompute(commandList, "Prefilter", environment->EnvironmentCube, nvrhi::TextureDimension::TextureCube, environment->Prefiltered, mip, true,
				&constants, sizeof(constants), size, size, 6);
		}

		SubmitAndWait(commandList);
		EMBER_CORE_INFO("Built environment '{}' (cube {}, irradiance {}, prefiltered {}x{} mips)",
			hdrPath.empty() ? std::string("procedural sky") : hdrPath, envSize, m_Settings.IrradianceSize, m_Settings.PrefilteredSize, prefilteredMips);
		return environment;
	}

}
