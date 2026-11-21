#include "Ember/Scene/SceneSerializer.h"

#include "Ember/Core/FileSystem.h"
#include "Ember/Core/Log.h"
#include "Ember/Core/YAMLUtils.h"
#include "Ember/Scene/ComponentRegistry.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Ember {

	namespace {

		YAML::Node SerializeEntity(Entity entity)
		{
			YAML::Node node;
			node["Entity"] = static_cast<uint64_t>(entity.GetUUID());
			const auto& relationship = entity.GetComponent<RelationshipComponent>();
			if (relationship.Parent.IsValid())
				node["Parent"] = static_cast<uint64_t>(relationship.Parent);
			if (!relationship.Children.empty())
			{
				YAML::Node children(YAML::NodeType::Sequence);
				for (UUID child : relationship.Children)
					children.push_back(static_cast<uint64_t>(child));
				children.SetStyle(YAML::EmitterStyle::Flow);
				node["Children"] = children;
			}
			node["Components"] = ComponentRegistry::SerializeEntityComponents(entity);
			return node;
		}

		std::string EmitDocument(const YAML::Node& root)
		{
			YAML::Emitter out;
			out.SetIndent(2);
			out.SetFloatPrecision(7);
			out << root;
			return std::string(out.c_str()) + "\n";
		}

		void CollectSubtree(Scene& scene, Entity entity, std::vector<Entity>& out)
		{
			out.push_back(entity);
			for (Entity child : scene.GetChildren(entity))
				CollectSubtree(scene, child, out);
		}

		struct ParsedEntity
		{
			UUID ID;
			UUID Parent;
			std::vector<UUID> Children;
			YAML::Node Components;
		};

		// Parses and validates an entity list. Returns false with an error message on malformed input.
		bool ParseEntities(const YAML::Node& entities, std::vector<ParsedEntity>& out, std::string& error)
		{
			if (!entities)
				return true; // empty scene
			if (!entities.IsSequence())
			{
				error = "'Entities' must be a sequence";
				return false;
			}

			std::unordered_set<uint64_t> seen;
			for (const YAML::Node& node : entities)
			{
				ParsedEntity parsed;
				uint64_t id = 0;
				if (!node.IsMap() || !ReadYAML(node, "Entity", id) || id == 0)
				{
					error = "entity entry without a valid 'Entity' id";
					return false;
				}
				if (!seen.insert(id).second)
				{
					error = "duplicate entity id " + std::to_string(id);
					return false;
				}
				parsed.ID = id;
				uint64_t parent = 0;
				ReadYAML(node, "Parent", parent);
				parsed.Parent = parent;
				if (const YAML::Node children = node["Children"]; children && children.IsSequence())
				{
					for (const YAML::Node& child : children)
						parsed.Children.push_back(child.as<uint64_t>());
				}
				// Note: assigning a missing key's (invalid) node to a YAML::Node throws; rebind with reset() instead.
				if (const YAML::Node components = node["Components"])
					parsed.Components.reset(components);
				if (parsed.Components && !parsed.Components.IsNull() && !parsed.Components.IsMap())
				{
					error = "'Components' of entity " + std::to_string(id) + " must be a map";
					return false;
				}
				out.push_back(std::move(parsed));
			}
			return true;
		}

		// Creates entities from parsed data. idRemap maps file ids to the ids to create (identity for scenes,
		// fresh ids for prefab instantiation). Relationships are rebuilt consistently from Parent links, using
		// the saved Children order where it agrees.
		std::vector<Entity> CreateEntities(Scene& scene, const std::vector<ParsedEntity>& parsed,
			const std::unordered_map<uint64_t, UUID>& idRemap)
		{
			std::vector<Entity> created;
			created.reserve(parsed.size());

			for (const ParsedEntity& p : parsed)
			{
				Entity entity = scene.CreateEntityWithUUID(idRemap.at(p.ID));
				if (p.Components && p.Components.IsMap())
				{
					for (const auto& item : p.Components)
					{
						const std::string name = item.first.as<std::string>();
						const ComponentInfo* info = ComponentRegistry::Find(name);
						if (!info)
						{
							EMBER_CORE_WARN("Unknown component '{}' on entity {} - skipped", name, static_cast<uint64_t>(p.ID));
							continue;
						}
						if (info->Name == "Relationship")
							continue;
						if (!info->Deserialize(entity, item.second))
							EMBER_CORE_WARN("Component '{}' on entity {} has malformed values", name, static_cast<uint64_t>(p.ID));
					}
				}
				created.push_back(entity);
			}

			// Relationships
			for (size_t i = 0; i < parsed.size(); i++)
			{
				const ParsedEntity& p = parsed[i];
				auto parentIt = idRemap.find(p.Parent);
				if (p.Parent.IsValid() && parentIt != idRemap.end())
					created[i].GetComponent<RelationshipComponent>().Parent = parentIt->second;
				else if (p.Parent.IsValid())
					EMBER_CORE_WARN("Entity {} references missing parent {} - made root", static_cast<uint64_t>(p.ID), static_cast<uint64_t>(p.Parent));
			}
			for (size_t i = 0; i < parsed.size(); i++)
			{
				Entity entity = created[i];
				const UUID entityID = entity.GetUUID();
				auto& children = entity.GetComponent<RelationshipComponent>().Children;
				children.clear();
				// Saved order first (only entries whose Parent agrees), then any others in file order.
				for (UUID savedChild : parsed[i].Children)
				{
					auto it = idRemap.find(savedChild);
					if (it == idRemap.end())
						continue;
					Entity child = scene.GetEntityByUUID(it->second);
					if (child && child.GetComponent<RelationshipComponent>().Parent == entityID &&
						std::find(children.begin(), children.end(), it->second) == children.end())
						children.push_back(it->second);
				}
				for (Entity other : created)
				{
					const UUID otherID = other.GetUUID();
					if (other.GetComponent<RelationshipComponent>().Parent == entityID &&
						std::find(children.begin(), children.end(), otherID) == children.end())
						children.push_back(otherID);
				}
			}

			// Break any cycles from corrupted files: an entity that is its own ancestor becomes a root.
			for (Entity entity : created)
			{
				if (scene.IsDescendantOf(entity, entity))
				{
					EMBER_CORE_WARN("Hierarchy cycle detected at entity '{}' - made root", entity.GetName());
					scene.SetParent(entity, Entity(), false);
				}
			}
			return created;
		}

	}

	std::string SceneSerializer::SerializeToString() const
	{
		YAML::Node root;
		root["Scene"] = m_Scene->GetName();
		root["Version"] = c_FormatVersion;
		YAML::Node entities(YAML::NodeType::Sequence);
		for (Entity entity : m_Scene->GetAllEntitiesOrdered())
			entities.push_back(SerializeEntity(entity));
		root["Entities"] = entities;
		return EmitDocument(root);
	}

	bool SceneSerializer::Serialize(const std::filesystem::path& path) const
	{
		return FileSystem::WriteText(path, SerializeToString());
	}

	bool SceneSerializer::Deserialize(const std::filesystem::path& path, std::string* outError)
	{
		std::optional<std::string> text = FileSystem::ReadText(path);
		if (!text)
		{
			const std::string error = "cannot read scene file '" + path.string() + "'";
			EMBER_CORE_ERROR("SceneSerializer: {}", error);
			if (outError)
				*outError = error;
			return false;
		}
		return DeserializeFromString(*text, outError);
	}

	bool SceneSerializer::DeserializeFromString(const std::string& text, std::string* outError)
	{
		std::string error;
		std::vector<ParsedEntity> parsed;
		std::string sceneName;
		try
		{
			const YAML::Node root = YAML::Load(text);
			if (!root.IsMap() || !root["Scene"])
			{
				error = "not a scene file (missing 'Scene' key)";
			}
			else
			{
				sceneName = root["Scene"].as<std::string>();
				int version = c_FormatVersion;
				ReadYAML(root, "Version", version);
				if (version > c_FormatVersion)
					error = "scene format version " + std::to_string(version) + " is newer than supported (" + std::to_string(c_FormatVersion) + ")";
				else
					ParseEntities(root["Entities"], parsed, error);
			}
		}
		catch (const YAML::Exception& e)
		{
			error = std::string("YAML parse error: ") + e.what();
		}

		if (!error.empty())
		{
			EMBER_CORE_ERROR("SceneSerializer: {}", error);
			if (outError)
				*outError = error;
			return false;
		}

		// Parsing succeeded: now it is safe to replace the scene contents.
		for (Entity entity : m_Scene->GetAllEntitiesOrdered())
		{
			if (entity.IsValid())
				m_Scene->DestroyEntityImmediate(entity);
		}
		m_Scene->m_Registry.clear();
		m_Scene->m_EntityMap.clear();
		m_Scene->m_NextCreationIndex = 1;
		m_Scene->SetName(sceneName);

		std::unordered_map<uint64_t, UUID> identity;
		for (const ParsedEntity& p : parsed)
			identity.emplace(static_cast<uint64_t>(p.ID), p.ID);
		CreateEntities(*m_Scene, parsed, identity);
		return true;
	}

	std::string SceneSerializer::SerializePrefabToString(Entity root)
	{
		if (!root)
			return {};
		Scene& scene = *root.GetScene();
		std::vector<Entity> subtree;
		CollectSubtree(scene, root, subtree);

		YAML::Node document;
		document["Prefab"] = root.GetName();
		document["Version"] = c_FormatVersion;
		YAML::Node entities(YAML::NodeType::Sequence);
		for (Entity entity : subtree)
		{
			YAML::Node node = SerializeEntity(entity);
			if (entity == root)
				node.remove("Parent"); // the prefab root is always a root
			entities.push_back(node);
		}
		document["Entities"] = entities;
		return EmitDocument(document);
	}

	bool SceneSerializer::SerializePrefab(Entity root, const std::filesystem::path& path)
	{
		if (!root)
			return false;
		return FileSystem::WriteText(path, SerializePrefabToString(root));
	}

	Entity SceneSerializer::InstantiatePrefabFromString(Scene& scene, const std::string& text, const std::string& prefabPath,
		std::string* outError)
	{
		std::string error;
		std::vector<ParsedEntity> parsed;
		try
		{
			const YAML::Node document = YAML::Load(text);
			if (!document.IsMap() || !document["Prefab"])
				error = "not a prefab file (missing 'Prefab' key)";
			else if (ParseEntities(document["Entities"], parsed, error) && parsed.empty())
				error = "prefab contains no entities";
		}
		catch (const YAML::Exception& e)
		{
			error = std::string("YAML parse error: ") + e.what();
		}

		if (!error.empty())
		{
			EMBER_CORE_ERROR("Prefab: {}", error);
			if (outError)
				*outError = error;
			return {};
		}

		// The first entity is the root; it must not reference a parent inside or outside the prefab.
		parsed[0].Parent = 0;

		std::unordered_map<uint64_t, UUID> remap;
		for (const ParsedEntity& p : parsed)
			remap.emplace(static_cast<uint64_t>(p.ID), UUID());

		std::vector<Entity> created = CreateEntities(scene, parsed, remap);
		Entity root = created.front();
		if (!prefabPath.empty())
			root.AddOrReplaceComponent<PrefabComponent>(PrefabComponent{ prefabPath });
		return root;
	}

	Entity SceneSerializer::InstantiatePrefab(Scene& scene, const std::filesystem::path& path, const std::string& prefabPath,
		std::string* outError)
	{
		std::optional<std::string> text = FileSystem::ReadText(path);
		if (!text)
		{
			const std::string error = "cannot read prefab '" + path.string() + "'";
			EMBER_CORE_ERROR("Prefab: {}", error);
			if (outError)
				*outError = error;
			return {};
		}
		return InstantiatePrefabFromString(scene, *text, prefabPath, outError);
	}

}
