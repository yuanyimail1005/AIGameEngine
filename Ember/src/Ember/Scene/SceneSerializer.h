#pragma once

#include "Ember/Core/Base.h"
#include "Ember/Scene/Entity.h"
#include "Ember/Scene/Scene.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Ember {

	// Reads and writes scenes (.escene) and prefabs (.eprefab) as human-readable YAML.
	// Unknown components are skipped with a warning (forward compatibility); malformed files fail cleanly
	// and leave the target scene untouched.
	class SceneSerializer
	{
	public:
		static constexpr int c_FormatVersion = 1;

		explicit SceneSerializer(const Ref<Scene>& scene) : m_Scene(scene) {}

		bool Serialize(const std::filesystem::path& path) const;
		std::string SerializeToString() const;

		// Replaces the scene's contents. On failure the scene is unchanged and outError (if given) is set.
		bool Deserialize(const std::filesystem::path& path, std::string* outError = nullptr);
		bool DeserializeFromString(const std::string& text, std::string* outError = nullptr);

		// --- Prefabs -----------------------------------------------------------------------------
		// Serializes an entity and all its descendants.
		static std::string SerializePrefabToString(Entity root);
		static bool SerializePrefab(Entity root, const std::filesystem::path& path);
		// Instantiates a prefab into the scene with fresh UUIDs. Returns the new root entity
		// (invalid on failure). The root gets a PrefabComponent recording prefabPath if non-empty.
		static Entity InstantiatePrefabFromString(Scene& scene, const std::string& text, const std::string& prefabPath = {},
			std::string* outError = nullptr);
		static Entity InstantiatePrefab(Scene& scene, const std::filesystem::path& path, const std::string& prefabPath = {},
			std::string* outError = nullptr);

	private:
		Ref<Scene> m_Scene;
	};

}
