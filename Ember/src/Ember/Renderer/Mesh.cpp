#include "Ember/Renderer/Mesh.h"

#include "Ember/Core/Log.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Ember {

	AABB MeshData::ComputeBounds()
	{
		AABB total{ glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest()) };
		for (Submesh& submesh : Submeshes)
		{
			AABB bounds{ glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest()) };
			for (uint32_t i = 0; i < submesh.IndexCount; i++)
			{
				const glm::vec3& p = Vertices[Indices[submesh.FirstIndex + i]].Position;
				bounds.Min = glm::min(bounds.Min, p);
				bounds.Max = glm::max(bounds.Max, p);
			}
			if (!bounds.IsValid())
				bounds = AABB{};
			submesh.Bounds = bounds;
			total.Min = glm::min(total.Min, bounds.Min);
			total.Max = glm::max(total.Max, bounds.Max);
		}
		return total.IsValid() ? total : AABB{};
	}

	void MeshData::GenerateNormals()
	{
		for (Vertex& v : Vertices)
			v.Normal = glm::vec3(0.0f);
		for (size_t i = 0; i + 2 < Indices.size(); i += 3)
		{
			Vertex& a = Vertices[Indices[i]];
			Vertex& b = Vertices[Indices[i + 1]];
			Vertex& c = Vertices[Indices[i + 2]];
			const glm::vec3 n = glm::cross(b.Position - a.Position, c.Position - a.Position); // area weighted
			a.Normal += n;
			b.Normal += n;
			c.Normal += n;
		}
		for (Vertex& v : Vertices)
			v.Normal = glm::length(v.Normal) > 1e-12f ? glm::normalize(v.Normal) : glm::vec3(0.0f, 1.0f, 0.0f);
	}

	void MeshData::GenerateTangents()
	{
		std::vector<glm::vec3> tangents(Vertices.size(), glm::vec3(0.0f));
		std::vector<glm::vec3> bitangents(Vertices.size(), glm::vec3(0.0f));
		for (size_t i = 0; i + 2 < Indices.size(); i += 3)
		{
			const uint32_t i0 = Indices[i], i1 = Indices[i + 1], i2 = Indices[i + 2];
			const Vertex& v0 = Vertices[i0];
			const Vertex& v1 = Vertices[i1];
			const Vertex& v2 = Vertices[i2];
			const glm::vec3 e1 = v1.Position - v0.Position;
			const glm::vec3 e2 = v2.Position - v0.Position;
			const glm::vec2 d1 = v1.TexCoord - v0.TexCoord;
			const glm::vec2 d2 = v2.TexCoord - v0.TexCoord;
			const float det = d1.x * d2.y - d2.x * d1.y;
			if (std::abs(det) < 1e-12f)
				continue;
			const float r = 1.0f / det;
			const glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;
			const glm::vec3 b = (e2 * d1.x - e1 * d2.x) * r;
			for (uint32_t index : { i0, i1, i2 })
			{
				tangents[index] += t;
				bitangents[index] += b;
			}
		}

		for (size_t i = 0; i < Vertices.size(); i++)
		{
			const glm::vec3& n = Vertices[i].Normal;
			glm::vec3 t = tangents[i] - n * glm::dot(n, tangents[i]);
			if (glm::length(t) < 1e-8f)
			{
				// Degenerate UVs: pick any vector perpendicular to the normal.
				t = std::abs(n.x) < 0.9f ? glm::cross(n, glm::vec3(1.0f, 0.0f, 0.0f)) : glm::cross(n, glm::vec3(0.0f, 1.0f, 0.0f));
			}
			t = glm::normalize(t);
			const float handedness = glm::dot(glm::cross(n, t), bitangents[i]) < 0.0f ? -1.0f : 1.0f;
			Vertices[i].Tangent = glm::vec4(t, handedness);
		}
	}

	bool MeshData::IsValid() const
	{
		if (Vertices.empty() || Indices.empty() || Indices.size() % 3 != 0 || Submeshes.empty())
			return false;
		for (uint32_t index : Indices)
		{
			if (index >= Vertices.size())
				return false;
		}
		for (const Submesh& submesh : Submeshes)
		{
			if (static_cast<size_t>(submesh.FirstIndex) + submesh.IndexCount > Indices.size())
				return false;
		}
		return true;
	}

	Ref<Mesh> Mesh::Create(nvrhi::IDevice* device, nvrhi::ICommandList* commandList, MeshData& data, const std::string& debugName)
	{
		if (!data.IsValid())
		{
			EMBER_CORE_ERROR("Mesh '{}': invalid mesh data", debugName);
			return nullptr;
		}

		auto mesh = CreateRef<Mesh>();
		mesh->Bounds = data.ComputeBounds();
		mesh->Submeshes = data.Submeshes;
		mesh->VertexCount = static_cast<uint32_t>(data.Vertices.size());
		mesh->IndexCount = static_cast<uint32_t>(data.Indices.size());

		nvrhi::BufferDesc vertexDesc;
		vertexDesc.byteSize = data.Vertices.size() * sizeof(Vertex);
		vertexDesc.isVertexBuffer = true;
		vertexDesc.debugName = debugName + " VB";
		vertexDesc.initialState = nvrhi::ResourceStates::VertexBuffer;
		vertexDesc.keepInitialState = true;
		mesh->VertexBuffer = device->createBuffer(vertexDesc);

		nvrhi::BufferDesc indexDesc;
		indexDesc.byteSize = data.Indices.size() * sizeof(uint32_t);
		indexDesc.isIndexBuffer = true;
		indexDesc.debugName = debugName + " IB";
		indexDesc.initialState = nvrhi::ResourceStates::IndexBuffer;
		indexDesc.keepInitialState = true;
		mesh->IndexBuffer = device->createBuffer(indexDesc);

		if (!mesh->VertexBuffer || !mesh->IndexBuffer)
			return nullptr;

		commandList->writeBuffer(mesh->VertexBuffer, data.Vertices.data(), vertexDesc.byteSize);
		commandList->writeBuffer(mesh->IndexBuffer, data.Indices.data(), indexDesc.byteSize);
		return mesh;
	}

	// ----------------------------------------------------------------------------------------------

	namespace MeshFactory {

		namespace {

			void FinalizeSingleSubmesh(MeshData& data)
			{
				data.Submeshes = { Submesh{ 0, static_cast<uint32_t>(data.Indices.size()), 0, {} } };
				data.GenerateTangents();
				data.ComputeBounds();
			}

		}

		MeshData CreateCube()
		{
			MeshData data;
			struct Face
			{
				glm::vec3 Normal, Right, Up;
			};
			// Each face: outward normal, and right/up axes as seen from outside (CCW winding).
			const Face faces[6] = {
				{ { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
				{ { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
				{ { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
				{ { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
				{ { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
				{ { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
			};
			for (const Face& face : faces)
			{
				const uint32_t base = static_cast<uint32_t>(data.Vertices.size());
				const glm::vec2 corners[4] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
				for (const glm::vec2& c : corners)
				{
					Vertex v;
					v.Position = 0.5f * (face.Normal + face.Right * c.x + face.Up * c.y);
					v.Normal = face.Normal;
					v.TexCoord = { (c.x + 1.0f) * 0.5f, 1.0f - (c.y + 1.0f) * 0.5f };
					data.Vertices.push_back(v);
				}
				data.Indices.insert(data.Indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
			}
			FinalizeSingleSubmesh(data);
			return data;
		}

		MeshData CreatePlane()
		{
			MeshData data;
			const glm::vec2 corners[4] = { { -0.5f, 0.5f }, { 0.5f, 0.5f }, { 0.5f, -0.5f }, { -0.5f, -0.5f } };
			for (const glm::vec2& c : corners)
			{
				Vertex v;
				v.Position = { c.x, 0.0f, c.y };
				v.Normal = { 0.0f, 1.0f, 0.0f };
				v.TexCoord = { c.x + 0.5f, c.y + 0.5f };
				data.Vertices.push_back(v);
			}
			data.Indices = { 0, 1, 2, 0, 2, 3 };
			FinalizeSingleSubmesh(data);
			return data;
		}

		MeshData CreateSphere(uint32_t segments, uint32_t rings)
		{
			MeshData data;
			segments = std::max(segments, 3u);
			rings = std::max(rings, 2u);
			for (uint32_t ring = 0; ring <= rings; ring++)
			{
				const float v = static_cast<float>(ring) / static_cast<float>(rings);
				const float phi = v * glm::pi<float>();
				for (uint32_t segment = 0; segment <= segments; segment++)
				{
					const float u = static_cast<float>(segment) / static_cast<float>(segments);
					const float theta = u * glm::two_pi<float>();
					Vertex vertex;
					vertex.Normal = { std::sin(phi) * std::sin(theta), std::cos(phi), std::sin(phi) * std::cos(theta) };
					vertex.Position = vertex.Normal * 0.5f;
					vertex.TexCoord = { u, v };
					data.Vertices.push_back(vertex);
				}
			}
			const uint32_t stride = segments + 1;
			for (uint32_t ring = 0; ring < rings; ring++)
			{
				for (uint32_t segment = 0; segment < segments; segment++)
				{
					const uint32_t a = ring * stride + segment;
					const uint32_t b = a + stride;
					if (ring != 0)
						data.Indices.insert(data.Indices.end(), { a, b, a + 1 });
					if (ring != rings - 1)
						data.Indices.insert(data.Indices.end(), { a + 1, b, b + 1 });
				}
			}
			FinalizeSingleSubmesh(data);
			return data;
		}

		MeshData CreateCylinder(uint32_t segments)
		{
			MeshData data;
			segments = std::max(segments, 3u);
			// Side
			for (uint32_t i = 0; i <= segments; i++)
			{
				const float u = static_cast<float>(i) / static_cast<float>(segments);
				const float theta = u * glm::two_pi<float>();
				const glm::vec3 normal(std::sin(theta), 0.0f, std::cos(theta));
				for (int side = 0; side < 2; side++)
				{
					Vertex v;
					v.Position = normal * 0.5f + glm::vec3(0.0f, side == 0 ? 0.5f : -0.5f, 0.0f);
					v.Normal = normal;
					v.TexCoord = { u, side == 0 ? 0.0f : 1.0f };
					data.Vertices.push_back(v);
				}
			}
			for (uint32_t i = 0; i < segments; i++)
			{
				const uint32_t top0 = i * 2, bottom0 = i * 2 + 1, top1 = i * 2 + 2, bottom1 = i * 2 + 3;
				data.Indices.insert(data.Indices.end(), { top0, bottom0, bottom1, top0, bottom1, top1 });
			}
			// Caps
			for (int cap = 0; cap < 2; cap++)
			{
				const float y = cap == 0 ? 0.5f : -0.5f;
				const glm::vec3 normal(0.0f, cap == 0 ? 1.0f : -1.0f, 0.0f);
				const uint32_t center = static_cast<uint32_t>(data.Vertices.size());
				Vertex c;
				c.Position = { 0.0f, y, 0.0f };
				c.Normal = normal;
				c.TexCoord = { 0.5f, 0.5f };
				data.Vertices.push_back(c);
				for (uint32_t i = 0; i <= segments; i++)
				{
					const float theta = static_cast<float>(i) / static_cast<float>(segments) * glm::two_pi<float>();
					Vertex v;
					v.Position = { std::sin(theta) * 0.5f, y, std::cos(theta) * 0.5f };
					v.Normal = normal;
					v.TexCoord = { std::sin(theta) * 0.5f + 0.5f, std::cos(theta) * 0.5f + 0.5f };
					data.Vertices.push_back(v);
				}
				for (uint32_t i = 0; i < segments; i++)
				{
					if (cap == 0)
						data.Indices.insert(data.Indices.end(), { center, center + 1 + i, center + 2 + i });
					else
						data.Indices.insert(data.Indices.end(), { center, center + 2 + i, center + 1 + i });
				}
			}
			FinalizeSingleSubmesh(data);
			return data;
		}

		MeshData CreateCapsule(uint32_t segments, uint32_t rings)
		{
			MeshData data;
			segments = std::max(segments, 3u);
			rings = std::max(rings, 2u);
			const float radius = 0.5f;
			const float halfHeight = 0.5f;
			// Rows: top hemisphere (rings), bottom hemisphere (rings), each with duplicated seam column.
			const uint32_t totalRows = rings * 2 + 1;
			for (uint32_t row = 0; row <= totalRows; row++)
			{
				const bool top = row <= rings;
				const float hemisphereT = top ? static_cast<float>(row) / static_cast<float>(rings)
					: static_cast<float>(row - 1 - rings) / static_cast<float>(rings);
				const float phi = top ? hemisphereT * glm::half_pi<float>() : glm::half_pi<float>() + hemisphereT * glm::half_pi<float>();
				const float yOffset = top ? halfHeight : -halfHeight;
				for (uint32_t segment = 0; segment <= segments; segment++)
				{
					const float u = static_cast<float>(segment) / static_cast<float>(segments);
					const float theta = u * glm::two_pi<float>();
					Vertex v;
					v.Normal = { std::sin(phi) * std::sin(theta), std::cos(phi), std::sin(phi) * std::cos(theta) };
					v.Position = v.Normal * radius + glm::vec3(0.0f, yOffset, 0.0f);
					v.TexCoord = { u, 0.5f - v.Position.y / (2.0f * (radius + halfHeight)) };
					data.Vertices.push_back(v);
				}
			}
			const uint32_t stride = segments + 1;
			for (uint32_t row = 0; row < totalRows; row++)
			{
				for (uint32_t segment = 0; segment < segments; segment++)
				{
					const uint32_t a = row * stride + segment;
					const uint32_t b = a + stride;
					if (row != 0)
						data.Indices.insert(data.Indices.end(), { a, b, a + 1 });
					if (row != totalRows - 1)
						data.Indices.insert(data.Indices.end(), { a + 1, b, b + 1 });
				}
			}
			FinalizeSingleSubmesh(data);
			return data;
		}

		MeshData CreateByName(const std::string& name)
		{
			if (name == "Cube")
				return CreateCube();
			if (name == "Plane")
				return CreatePlane();
			if (name == "Sphere")
				return CreateSphere();
			if (name == "Cylinder")
				return CreateCylinder();
			if (name == "Capsule")
				return CreateCapsule();
			return {};
		}

	}

}
