#pragma once

#include "Ember/Core/UUID.h"
#include "Ember/Renderer/MaterialParameters.h"
#include "Ember/Scene/SceneCamera.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <string>
#include <vector>

// All scene components. Components are plain data; behaviour lives in Scene systems.
// When adding a component: register it in ComponentRegistry.cpp (serialization + editor/AI access),
// add it to the test scene, and add tests. See AGENTS.md.

namespace Ember {

	struct IDComponent
	{
		UUID ID;
		// Runtime only: monotonically increasing creation order, used for deterministic ordering
		// (hierarchy panel, serialization). Reassigned in file order when a scene is loaded.
		uint64_t CreationIndex = 0;
	};

	struct TagComponent
	{
		std::string Tag;
	};

	struct TransformComponent
	{
		glm::vec3 Translation{ 0.0f };
		glm::vec3 Rotation{ 0.0f }; // Euler angles in radians (XYZ order: pitch, yaw, roll)
		glm::vec3 Scale{ 1.0f };

		glm::quat GetRotationQuat() const { return glm::quat(Rotation); }
		void SetRotationQuat(const glm::quat& rotation) { Rotation = glm::eulerAngles(rotation); }

		// Local transform (relative to parent).
		glm::mat4 GetTransform() const
		{
			return glm::translate(glm::mat4(1.0f), Translation) * glm::toMat4(GetRotationQuat()) * glm::scale(glm::mat4(1.0f), Scale);
		}
	};

	// Parent/child hierarchy. Children are kept in order (editor display, deterministic serialization).
	struct RelationshipComponent
	{
		UUID Parent = 0;
		std::vector<UUID> Children;
	};

	struct CameraComponent
	{
		SceneCamera Camera;
		bool Primary = true;
		bool FixedAspectRatio = false;
	};

	struct MeshRendererComponent
	{
		// Asset path of a glTF model, or a built-in primitive: builtin://Cube, Sphere, Plane, Capsule, Cylinder.
		std::string Mesh;
		// Optional material asset (.ematerial) applied to all submeshes.
		std::string Material;
		// When enabled, these parameters override the model / material asset entirely.
		bool OverrideMaterial = false;
		MaterialParameters MaterialOverride;
		bool CastShadows = true;
		bool Visible = true;
	};

	struct DirectionalLightComponent
	{
		glm::vec3 Color{ 1.0f };
		float Intensity = 3.0f;
		bool CastShadows = true;
		// Light size used for PCSS-style soft shadows (larger = softer penumbrae).
		float ShadowSoftness = 1.0f;
	};

	struct PointLightComponent
	{
		glm::vec3 Color{ 1.0f };
		float Intensity = 10.0f;
		float Range = 10.0f;
	};

	struct SpotLightComponent
	{
		glm::vec3 Color{ 1.0f };
		float Intensity = 20.0f;
		float Range = 15.0f;
		float InnerConeAngle = 0.35f; // radians
		float OuterConeAngle = 0.6f;  // radians
	};

	// Image-based lighting from an equirectangular HDRI (e.g. from polyhaven.com). Also drawn as the skybox.
	struct SkyLightComponent
	{
		std::string EnvironmentMap; // asset path to a .hdr file; empty = procedural sky gradient
		float Intensity = 1.0f;
		float Rotation = 0.0f; // radians around +Y
		bool DrawSkybox = true;
	};

	enum class RigidBodyType : uint8_t
	{
		Static = 0, Dynamic, Kinematic
	};

	struct RigidBodyComponent
	{
		RigidBodyType Type = RigidBodyType::Dynamic;
		float Mass = 1.0f;
		float LinearDamping = 0.05f;
		float AngularDamping = 0.05f;
		float GravityScale = 1.0f;
		float Friction = 0.5f;
		float Restitution = 0.0f;
		bool IsTrigger = false;      // Sensor: reports overlaps, no collision response
		bool LockRotation = false;   // Keeps the body upright (characters)
		bool ContinuousDetection = false;
	};

	// Colliders: an entity may have several (combined into one compound shape). An entity with colliders but no
	// RigidBodyComponent is a static collider. Collider sizes are multiplied by the entity's world scale.
	struct BoxColliderComponent
	{
		glm::vec3 HalfExtents{ 0.5f };
		glm::vec3 Offset{ 0.0f };
	};

	struct SphereColliderComponent
	{
		float Radius = 0.5f;
		glm::vec3 Offset{ 0.0f };
	};

	struct CapsuleColliderComponent
	{
		float Radius = 0.5f;
		float HalfHeight = 0.5f; // half height of the cylindrical part (Y axis)
		glm::vec3 Offset{ 0.0f };
	};

	struct ScriptComponent
	{
		std::string Script; // asset path to a .lua file
	};

	struct AudioSourceComponent
	{
		std::string Clip; // asset path to .wav / .mp3 / .flac / .ogg
		float Volume = 1.0f;
		float Pitch = 1.0f;
		bool Loop = false;
		bool PlayOnStart = false;
		bool Spatial = true;
		float MinDistance = 1.0f;
		float MaxDistance = 50.0f;
	};

	struct AudioListenerComponent
	{
		bool Active = true;
	};

	// Present on the root entity of an instantiated prefab (records its source for the editor).
	struct PrefabComponent
	{
		std::string Prefab; // asset path to the .eprefab
	};

	template<typename... Component>
	struct ComponentGroup
	{
	};

	// Every component that is copied when duplicating entities or scenes (IDComponent and TagComponent are
	// handled explicitly). Keep in sync with ComponentRegistry.cpp.
	using AllComponents = ComponentGroup<
		TransformComponent, RelationshipComponent, CameraComponent, MeshRendererComponent,
		DirectionalLightComponent, PointLightComponent, SpotLightComponent, SkyLightComponent,
		RigidBodyComponent, BoxColliderComponent, SphereColliderComponent, CapsuleColliderComponent,
		ScriptComponent, AudioSourceComponent, AudioListenerComponent, PrefabComponent>;

}
