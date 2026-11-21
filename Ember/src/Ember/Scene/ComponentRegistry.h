#pragma once

#include "Ember/Scene/Entity.h"

#include <yaml-cpp/yaml.h>

#include <string>
#include <string_view>
#include <vector>

namespace Ember {

	// Single source of truth for per-component metadata: names, add/remove, and conversion to/from YAML nodes.
	// Used by the scene serializer, prefab system, editor inspector and AI control interface, so every
	// component property is accessible everywhere without duplicating code.
	//
	// Deserialize has *patch* semantics: only keys present in the node are written, everything else keeps its
	// current value. Serialize always writes every field.
	struct ComponentInfo
	{
		std::string Name;     // Short name, e.g. "Transform" (also accepted: "TransformComponent")
		std::string Category; // Editor grouping, e.g. "Rendering"
		bool UserAddable = true; // false for built-in components every entity has (ID, Tag, Transform, Relationship)

		bool (*Has)(Entity) = nullptr;
		void (*Add)(Entity) = nullptr;     // adds with default values (no-op if present)
		void (*Remove)(Entity) = nullptr;  // no-op if absent
		YAML::Node (*Serialize)(Entity) = nullptr;
		// Adds the component if missing, then applies the patch. Returns false on malformed values
		// (fields that parsed correctly are still applied).
		bool (*Deserialize)(Entity, const YAML::Node&) = nullptr;
	};

	class ComponentRegistry
	{
	public:
		// All registered components in display / serialization order.
		static const std::vector<ComponentInfo>& GetAll();
		// Case-insensitive lookup by short name or full name ("Transform" / "TransformComponent").
		static const ComponentInfo* Find(std::string_view name);

		// Serializes all components on the entity (except ID and Relationship) into a map keyed by short name.
		static YAML::Node SerializeEntityComponents(Entity entity);
	};

}
