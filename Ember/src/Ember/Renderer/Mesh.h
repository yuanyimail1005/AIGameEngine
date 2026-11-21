#pragma once

#include "Ember/Core/Base.h"
#include "Ember/Renderer/MaterialParameters.h"
#include "Ember/Renderer/RenderTypes.h"

#include <nvrhi/nvrhi.h>

#include <string>
#include <vector>

namespace Ember {

	struct Submesh
	{
		uint32_t FirstIndex = 0;
		uint32_t IndexCount = 0;
		uint32_t MaterialIndex = 0; // index into Model::Materials
		AABB Bounds;
	};

	// CPU-side mesh data (all submeshes share one vertex and one index buffer).
	struct MeshData
	{
		std::vector<Vertex> Vertices;
		std::vector<uint32_t> Indices;
		std::vector<Submesh> Submeshes;

		// Recomputes per-submesh and total bounds from the vertices.
		AABB ComputeBounds();
		// Computes tangents from positions, normals and UVs (per-triangle accumulation, Gram-Schmidt).
		void GenerateTangents();
		// Recomputes smooth normals from triangles (used when a model has none).
		void GenerateNormals();
		bool IsValid() const;
	};

	// GPU mesh.
	struct Mesh
	{
		nvrhi::BufferHandle VertexBuffer;
		nvrhi::BufferHandle IndexBuffer;
		std::vector<Submesh> Submeshes;
		AABB Bounds;
		uint32_t VertexCount = 0;
		uint32_t IndexCount = 0;

		// Creates GPU buffers and uploads the data through the command list. Returns nullptr on invalid data.
		static Ref<Mesh> Create(nvrhi::IDevice* device, nvrhi::ICommandList* commandList, MeshData& data, const std::string& debugName);
	};

	// A renderable asset: a mesh plus the materials its submeshes reference.
	struct Model
	{
		std::string Path;
		Ref<Mesh> MeshData;
		std::vector<MaterialParameters> Materials;
	};

	// Built-in primitives, all centered at the origin with unit size (1 x 1 x 1 bounds unless noted).
	namespace MeshFactory {
		MeshData CreateCube();
		MeshData CreatePlane();                                   // 1 x 1 in XZ, facing +Y
		MeshData CreateSphere(uint32_t segments = 32, uint32_t rings = 16);     // radius 0.5
		MeshData CreateCylinder(uint32_t segments = 32);          // radius 0.5, height 1
		MeshData CreateCapsule(uint32_t segments = 32, uint32_t rings = 8);     // radius 0.5, total height 2

		// Returns the primitive for a built-in name ("Cube", "Plane", "Sphere", "Cylinder", "Capsule"), or empty data.
		MeshData CreateByName(const std::string& name);
	}

}
