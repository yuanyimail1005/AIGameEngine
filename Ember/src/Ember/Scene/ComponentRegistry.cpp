#include "Ember/Scene/ComponentRegistry.h"

#include "Ember/Core/YAMLUtils.h"
#include "Ember/Renderer/MaterialSerializer.h"

#include <algorithm>
#include <cctype>

namespace Ember {

	namespace {

		bool EqualsIgnoreCase(std::string_view a, std::string_view b)
		{
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y)
			{
				return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
			});
		}

		// ------------------------------------------------------------------------------------------
		// Enum <-> string

		template<typename E>
		struct EnumName
		{
			E Value;
			const char* Name;
		};

		constexpr EnumName<RigidBodyType> s_BodyTypes[] = {
			{ RigidBodyType::Static, "Static" }, { RigidBodyType::Dynamic, "Dynamic" }, { RigidBodyType::Kinematic, "Kinematic" } };
		constexpr EnumName<SceneCamera::ProjectionType> s_ProjectionTypes[] = {
			{ SceneCamera::ProjectionType::Perspective, "Perspective" }, { SceneCamera::ProjectionType::Orthographic, "Orthographic" } };

		template<typename E, size_t N>
		const char* EnumToString(const EnumName<E> (&table)[N], E value)
		{
			for (const auto& entry : table)
			{
				if (entry.Value == value)
					return entry.Name;
			}
			return table[0].Name;
		}

		// Reads an enum stored as a string. Returns false (value untouched) if present but unrecognized.
		template<typename E, size_t N>
		bool ReadEnum(const YAML::Node& node, const char* key, const EnumName<E> (&table)[N], E& value)
		{
			const YAML::Node child = node[key];
			if (!child)
				return true;
			if (!child.IsScalar())
				return false;
			const std::string text = child.Scalar();
			for (const auto& entry : table)
			{
				if (EqualsIgnoreCase(text, entry.Name))
				{
					value = entry.Value;
					return true;
				}
			}
			return false;
		}

		// Reads a field; returns false only if the key is present but has the wrong type.
		template<typename T>
		bool Read(const YAML::Node& node, const char* key, T& value)
		{
			const YAML::Node child = node[key];
			if (!child)
				return true;
			return ReadYAML(node, key, value);
		}

		// ------------------------------------------------------------------------------------------
		// Per-component conversion

		YAML::Node ToNode(const TagComponent& c)
		{
			YAML::Node n;
			n["Tag"] = c.Tag;
			return n;
		}

		bool FromNode(const YAML::Node& n, TagComponent& c)
		{
			return Read(n, "Tag", c.Tag);
		}

		YAML::Node ToNode(const TransformComponent& c)
		{
			YAML::Node n;
			n["Translation"] = c.Translation;
			n["Rotation"] = c.Rotation;
			n["Scale"] = c.Scale;
			return n;
		}

		bool FromNode(const YAML::Node& n, TransformComponent& c)
		{
			bool ok = Read(n, "Translation", c.Translation);
			ok &= Read(n, "Rotation", c.Rotation);
			ok &= Read(n, "Scale", c.Scale);
			// Convenience for scripts / AI: rotation in degrees.
			glm::vec3 degrees;
			if (n["RotationDegrees"])
			{
				if (ReadYAML(n, "RotationDegrees", degrees))
					c.Rotation = glm::radians(degrees);
				else
					ok = false;
			}
			return ok;
		}

		YAML::Node ToNode(const RelationshipComponent& c)
		{
			YAML::Node n;
			n["Parent"] = static_cast<uint64_t>(c.Parent);
			YAML::Node children(YAML::NodeType::Sequence);
			for (UUID child : c.Children)
				children.push_back(static_cast<uint64_t>(child));
			children.SetStyle(YAML::EmitterStyle::Flow);
			n["Children"] = children;
			return n;
		}

		bool FromNode(const YAML::Node& n, RelationshipComponent& c)
		{
			bool ok = true;
			uint64_t parent = static_cast<uint64_t>(c.Parent);
			ok &= Read(n, "Parent", parent);
			c.Parent = parent;
			if (const YAML::Node children = n["Children"]; children && children.IsSequence())
			{
				c.Children.clear();
				for (const YAML::Node& child : children)
					c.Children.push_back(child.as<uint64_t>());
			}
			return ok;
		}

		YAML::Node ToNode(const CameraComponent& c)
		{
			YAML::Node n;
			const SceneCamera& cam = c.Camera;
			n["ProjectionType"] = EnumToString(s_ProjectionTypes, cam.GetProjectionType());
			n["PerspectiveFOV"] = cam.GetPerspectiveVerticalFOV();
			n["PerspectiveNear"] = cam.GetPerspectiveNearClip();
			n["PerspectiveFar"] = cam.GetPerspectiveFarClip();
			n["OrthographicSize"] = cam.GetOrthographicSize();
			n["OrthographicNear"] = cam.GetOrthographicNearClip();
			n["OrthographicFar"] = cam.GetOrthographicFarClip();
			n["Primary"] = c.Primary;
			n["FixedAspectRatio"] = c.FixedAspectRatio;
			return n;
		}

		bool FromNode(const YAML::Node& n, CameraComponent& c)
		{
			SceneCamera& cam = c.Camera;
			bool ok = true;
			SceneCamera::ProjectionType type = cam.GetProjectionType();
			ok &= ReadEnum(n, "ProjectionType", s_ProjectionTypes, type);
			float fov = cam.GetPerspectiveVerticalFOV(), pNear = cam.GetPerspectiveNearClip(), pFar = cam.GetPerspectiveFarClip();
			float size = cam.GetOrthographicSize(), oNear = cam.GetOrthographicNearClip(), oFar = cam.GetOrthographicFarClip();
			ok &= Read(n, "PerspectiveFOV", fov);
			if (n["PerspectiveFOVDegrees"])
			{
				float degrees = 0.0f;
				if (ReadYAML(n, "PerspectiveFOVDegrees", degrees))
					fov = glm::radians(degrees);
				else
					ok = false;
			}
			ok &= Read(n, "PerspectiveNear", pNear);
			ok &= Read(n, "PerspectiveFar", pFar);
			ok &= Read(n, "OrthographicSize", size);
			ok &= Read(n, "OrthographicNear", oNear);
			ok &= Read(n, "OrthographicFar", oFar);
			cam.SetPerspectiveVerticalFOV(fov);
			cam.SetPerspectiveNearClip(pNear);
			cam.SetPerspectiveFarClip(pFar);
			cam.SetOrthographicSize(size);
			cam.SetOrthographicNearClip(oNear);
			cam.SetOrthographicFarClip(oFar);
			cam.SetProjectionType(type);
			ok &= Read(n, "Primary", c.Primary);
			ok &= Read(n, "FixedAspectRatio", c.FixedAspectRatio);
			return ok;
		}

		YAML::Node ToNode(const MeshRendererComponent& c)
		{
			YAML::Node n;
			n["Mesh"] = c.Mesh;
			n["Material"] = c.Material;
			n["OverrideMaterial"] = c.OverrideMaterial;
			n["MaterialOverride"] = MaterialSerializer::ToYAML(c.MaterialOverride);
			n["CastShadows"] = c.CastShadows;
			n["Visible"] = c.Visible;
			return n;
		}

		bool FromNode(const YAML::Node& n, MeshRendererComponent& c)
		{
			bool ok = Read(n, "Mesh", c.Mesh);
			ok &= Read(n, "Material", c.Material);
			ok &= Read(n, "OverrideMaterial", c.OverrideMaterial);
			if (const YAML::Node material = n["MaterialOverride"]; material && material.IsMap())
			{
				ok &= MaterialSerializer::FromYAML(material, c.MaterialOverride);
				// Patching the override implies wanting it used, unless explicitly stated otherwise.
				if (!n["OverrideMaterial"])
					c.OverrideMaterial = true;
			}
			ok &= Read(n, "CastShadows", c.CastShadows);
			ok &= Read(n, "Visible", c.Visible);
			return ok;
		}

		YAML::Node ToNode(const DirectionalLightComponent& c)
		{
			YAML::Node n;
			n["Color"] = c.Color;
			n["Intensity"] = c.Intensity;
			n["CastShadows"] = c.CastShadows;
			n["ShadowSoftness"] = c.ShadowSoftness;
			return n;
		}

		bool FromNode(const YAML::Node& n, DirectionalLightComponent& c)
		{
			bool ok = Read(n, "Color", c.Color);
			ok &= Read(n, "Intensity", c.Intensity);
			ok &= Read(n, "CastShadows", c.CastShadows);
			ok &= Read(n, "ShadowSoftness", c.ShadowSoftness);
			return ok;
		}

		YAML::Node ToNode(const PointLightComponent& c)
		{
			YAML::Node n;
			n["Color"] = c.Color;
			n["Intensity"] = c.Intensity;
			n["Range"] = c.Range;
			return n;
		}

		bool FromNode(const YAML::Node& n, PointLightComponent& c)
		{
			bool ok = Read(n, "Color", c.Color);
			ok &= Read(n, "Intensity", c.Intensity);
			ok &= Read(n, "Range", c.Range);
			return ok;
		}

		YAML::Node ToNode(const SpotLightComponent& c)
		{
			YAML::Node n;
			n["Color"] = c.Color;
			n["Intensity"] = c.Intensity;
			n["Range"] = c.Range;
			n["InnerConeAngle"] = c.InnerConeAngle;
			n["OuterConeAngle"] = c.OuterConeAngle;
			return n;
		}

		bool FromNode(const YAML::Node& n, SpotLightComponent& c)
		{
			bool ok = Read(n, "Color", c.Color);
			ok &= Read(n, "Intensity", c.Intensity);
			ok &= Read(n, "Range", c.Range);
			ok &= Read(n, "InnerConeAngle", c.InnerConeAngle);
			ok &= Read(n, "OuterConeAngle", c.OuterConeAngle);
			return ok;
		}

		YAML::Node ToNode(const SkyLightComponent& c)
		{
			YAML::Node n;
			n["EnvironmentMap"] = c.EnvironmentMap;
			n["Intensity"] = c.Intensity;
			n["Rotation"] = c.Rotation;
			n["DrawSkybox"] = c.DrawSkybox;
			return n;
		}

		bool FromNode(const YAML::Node& n, SkyLightComponent& c)
		{
			bool ok = Read(n, "EnvironmentMap", c.EnvironmentMap);
			ok &= Read(n, "Intensity", c.Intensity);
			ok &= Read(n, "Rotation", c.Rotation);
			ok &= Read(n, "DrawSkybox", c.DrawSkybox);
			return ok;
		}

		YAML::Node ToNode(const RigidBodyComponent& c)
		{
			YAML::Node n;
			n["Type"] = EnumToString(s_BodyTypes, c.Type);
			n["Mass"] = c.Mass;
			n["LinearDamping"] = c.LinearDamping;
			n["AngularDamping"] = c.AngularDamping;
			n["GravityScale"] = c.GravityScale;
			n["Friction"] = c.Friction;
			n["Restitution"] = c.Restitution;
			n["IsTrigger"] = c.IsTrigger;
			n["LockRotation"] = c.LockRotation;
			n["ContinuousDetection"] = c.ContinuousDetection;
			return n;
		}

		bool FromNode(const YAML::Node& n, RigidBodyComponent& c)
		{
			bool ok = ReadEnum(n, "Type", s_BodyTypes, c.Type);
			ok &= Read(n, "Mass", c.Mass);
			ok &= Read(n, "LinearDamping", c.LinearDamping);
			ok &= Read(n, "AngularDamping", c.AngularDamping);
			ok &= Read(n, "GravityScale", c.GravityScale);
			ok &= Read(n, "Friction", c.Friction);
			ok &= Read(n, "Restitution", c.Restitution);
			ok &= Read(n, "IsTrigger", c.IsTrigger);
			ok &= Read(n, "LockRotation", c.LockRotation);
			ok &= Read(n, "ContinuousDetection", c.ContinuousDetection);
			return ok;
		}

		YAML::Node ToNode(const BoxColliderComponent& c)
		{
			YAML::Node n;
			n["HalfExtents"] = c.HalfExtents;
			n["Offset"] = c.Offset;
			return n;
		}

		bool FromNode(const YAML::Node& n, BoxColliderComponent& c)
		{
			bool ok = Read(n, "HalfExtents", c.HalfExtents);
			ok &= Read(n, "Offset", c.Offset);
			return ok;
		}

		YAML::Node ToNode(const SphereColliderComponent& c)
		{
			YAML::Node n;
			n["Radius"] = c.Radius;
			n["Offset"] = c.Offset;
			return n;
		}

		bool FromNode(const YAML::Node& n, SphereColliderComponent& c)
		{
			bool ok = Read(n, "Radius", c.Radius);
			ok &= Read(n, "Offset", c.Offset);
			return ok;
		}

		YAML::Node ToNode(const CapsuleColliderComponent& c)
		{
			YAML::Node n;
			n["Radius"] = c.Radius;
			n["HalfHeight"] = c.HalfHeight;
			n["Offset"] = c.Offset;
			return n;
		}

		bool FromNode(const YAML::Node& n, CapsuleColliderComponent& c)
		{
			bool ok = Read(n, "Radius", c.Radius);
			ok &= Read(n, "HalfHeight", c.HalfHeight);
			ok &= Read(n, "Offset", c.Offset);
			return ok;
		}

		YAML::Node ToNode(const ScriptComponent& c)
		{
			YAML::Node n;
			n["Script"] = c.Script;
			return n;
		}

		bool FromNode(const YAML::Node& n, ScriptComponent& c)
		{
			return Read(n, "Script", c.Script);
		}

		YAML::Node ToNode(const AudioSourceComponent& c)
		{
			YAML::Node n;
			n["Clip"] = c.Clip;
			n["Volume"] = c.Volume;
			n["Pitch"] = c.Pitch;
			n["Loop"] = c.Loop;
			n["PlayOnStart"] = c.PlayOnStart;
			n["Spatial"] = c.Spatial;
			n["MinDistance"] = c.MinDistance;
			n["MaxDistance"] = c.MaxDistance;
			return n;
		}

		bool FromNode(const YAML::Node& n, AudioSourceComponent& c)
		{
			bool ok = Read(n, "Clip", c.Clip);
			ok &= Read(n, "Volume", c.Volume);
			ok &= Read(n, "Pitch", c.Pitch);
			ok &= Read(n, "Loop", c.Loop);
			ok &= Read(n, "PlayOnStart", c.PlayOnStart);
			ok &= Read(n, "Spatial", c.Spatial);
			ok &= Read(n, "MinDistance", c.MinDistance);
			ok &= Read(n, "MaxDistance", c.MaxDistance);
			return ok;
		}

		YAML::Node ToNode(const AudioListenerComponent& c)
		{
			YAML::Node n;
			n["Active"] = c.Active;
			return n;
		}

		bool FromNode(const YAML::Node& n, AudioListenerComponent& c)
		{
			return Read(n, "Active", c.Active);
		}

		YAML::Node ToNode(const PrefabComponent& c)
		{
			YAML::Node n;
			n["Prefab"] = c.Prefab;
			return n;
		}

		bool FromNode(const YAML::Node& n, PrefabComponent& c)
		{
			return Read(n, "Prefab", c.Prefab);
		}

		// ------------------------------------------------------------------------------------------

		template<typename T>
		ComponentInfo MakeInfo(const char* name, const char* category, bool userAddable = true)
		{
			ComponentInfo info;
			info.Name = name;
			info.Category = category;
			info.UserAddable = userAddable;
			info.Has = [](Entity e) { return e.HasComponent<T>(); };
			info.Add = [](Entity e) { if (!e.HasComponent<T>()) e.AddComponent<T>(); };
			info.Remove = [](Entity e) { if (e.HasComponent<T>()) e.RemoveComponent<T>(); };
			info.Serialize = [](Entity e) -> YAML::Node
			{
				if (!e.HasComponent<T>())
					return YAML::Node();
				return ToNode(e.GetComponent<T>());
			};
			info.Deserialize = [](Entity e, const YAML::Node& node) -> bool
			{
				T& component = e.HasComponent<T>() ? e.GetComponent<T>() : e.AddComponent<T>();
				if (!node || node.IsNull())
					return true;
				if (!node.IsMap())
					return false;
				try
				{
					return FromNode(node, component);
				}
				catch (const YAML::Exception&)
				{
					return false;
				}
			};
			return info;
		}

		std::vector<ComponentInfo> BuildRegistry()
		{
			std::vector<ComponentInfo> infos;
			infos.push_back(MakeInfo<TagComponent>("Tag", "Core", false));
			infos.push_back(MakeInfo<TransformComponent>("Transform", "Core", false));
			infos.push_back(MakeInfo<RelationshipComponent>("Relationship", "Core", false));
			infos.push_back(MakeInfo<CameraComponent>("Camera", "Rendering"));
			infos.push_back(MakeInfo<MeshRendererComponent>("MeshRenderer", "Rendering"));
			infos.push_back(MakeInfo<DirectionalLightComponent>("DirectionalLight", "Lighting"));
			infos.push_back(MakeInfo<PointLightComponent>("PointLight", "Lighting"));
			infos.push_back(MakeInfo<SpotLightComponent>("SpotLight", "Lighting"));
			infos.push_back(MakeInfo<SkyLightComponent>("SkyLight", "Lighting"));
			infos.push_back(MakeInfo<RigidBodyComponent>("RigidBody", "Physics"));
			infos.push_back(MakeInfo<BoxColliderComponent>("BoxCollider", "Physics"));
			infos.push_back(MakeInfo<SphereColliderComponent>("SphereCollider", "Physics"));
			infos.push_back(MakeInfo<CapsuleColliderComponent>("CapsuleCollider", "Physics"));
			infos.push_back(MakeInfo<ScriptComponent>("Script", "Scripting"));
			infos.push_back(MakeInfo<AudioSourceComponent>("AudioSource", "Audio"));
			infos.push_back(MakeInfo<AudioListenerComponent>("AudioListener", "Audio"));
			infos.push_back(MakeInfo<PrefabComponent>("Prefab", "Core", false));
			return infos;
		}

	}

	const std::vector<ComponentInfo>& ComponentRegistry::GetAll()
	{
		static const std::vector<ComponentInfo> s_Registry = BuildRegistry();
		return s_Registry;
	}

	const ComponentInfo* ComponentRegistry::Find(std::string_view name)
	{
		constexpr std::string_view suffix = "Component";
		if (name.size() > suffix.size() && EqualsIgnoreCase(name.substr(name.size() - suffix.size()), suffix))
			name = name.substr(0, name.size() - suffix.size());

		for (const ComponentInfo& info : GetAll())
		{
			if (EqualsIgnoreCase(info.Name, name))
				return &info;
		}
		return nullptr;
	}

	YAML::Node ComponentRegistry::SerializeEntityComponents(Entity entity)
	{
		YAML::Node node(YAML::NodeType::Map);
		for (const ComponentInfo& info : GetAll())
		{
			if (info.Name == "Relationship")
				continue;
			if (info.Has(entity))
				node[info.Name] = info.Serialize(entity);
		}
		return node;
	}

}
