#pragma once

#include "Ember/Core/UUID.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <yaml-cpp/yaml.h>

#include <string>

// yaml-cpp conversions and emitter helpers for Ember / glm types.
// Vectors are encoded as flow sequences: [x, y, z]. JSON arrays parse identically (JSON is valid YAML).

namespace YAML {

	template<>
	struct convert<glm::vec2>
	{
		static Node encode(const glm::vec2& v)
		{
			Node node;
			node.push_back(v.x); node.push_back(v.y);
			node.SetStyle(EmitterStyle::Flow);
			return node;
		}

		static bool decode(const Node& node, glm::vec2& v)
		{
			if (!node.IsSequence() || node.size() != 2)
				return false;
			v.x = node[0].as<float>(); v.y = node[1].as<float>();
			return true;
		}
	};

	template<>
	struct convert<glm::vec3>
	{
		static Node encode(const glm::vec3& v)
		{
			Node node;
			node.push_back(v.x); node.push_back(v.y); node.push_back(v.z);
			node.SetStyle(EmitterStyle::Flow);
			return node;
		}

		static bool decode(const Node& node, glm::vec3& v)
		{
			if (!node.IsSequence() || node.size() != 3)
				return false;
			v.x = node[0].as<float>(); v.y = node[1].as<float>(); v.z = node[2].as<float>();
			return true;
		}
	};

	template<>
	struct convert<glm::vec4>
	{
		static Node encode(const glm::vec4& v)
		{
			Node node;
			node.push_back(v.x); node.push_back(v.y); node.push_back(v.z); node.push_back(v.w);
			node.SetStyle(EmitterStyle::Flow);
			return node;
		}

		static bool decode(const Node& node, glm::vec4& v)
		{
			if (!node.IsSequence() || node.size() != 4)
				return false;
			v.x = node[0].as<float>(); v.y = node[1].as<float>(); v.z = node[2].as<float>(); v.w = node[3].as<float>();
			return true;
		}
	};

	template<>
	struct convert<Ember::UUID>
	{
		static Node encode(const Ember::UUID& uuid)
		{
			return Node(static_cast<uint64_t>(uuid));
		}

		static bool decode(const Node& node, Ember::UUID& uuid)
		{
			uuid = node.as<uint64_t>();
			return true;
		}
	};

	Emitter& operator<<(Emitter& out, const glm::vec2& v);
	Emitter& operator<<(Emitter& out, const glm::vec3& v);
	Emitter& operator<<(Emitter& out, const glm::vec4& v);

}

namespace Ember {

	// Scalar node that generic converters (JSON for the AI interface, Lua tables) must treat as a string even when the
	// text looks like a number or boolean (e.g. a UI text "42"). Uses the YAML non-specific tag "!", which the parser
	// also gives quoted scalars and the emitter does not write, so the file format is unchanged.
	inline YAML::Node MakeStringNode(const std::string& text)
	{
		YAML::Node node(text);
		node.SetTag("!");
		return node;
	}

	inline bool IsStringNode(const YAML::Node& node)
	{
		return node.IsScalar() && node.Tag() == "!";
	}

	// Reads node[key] into value if present and convertible. Returns true if the value was read.
	// Missing keys leave value untouched, which is what makes partial updates ("patches") work.
	template<typename T>
	bool ReadYAML(const YAML::Node& node, const char* key, T& value)
	{
		const YAML::Node child = node[key];
		if (!child || child.IsNull())
			return false;
		try
		{
			value = child.as<T>();
			return true;
		}
		catch (const YAML::Exception&)
		{
			return false;
		}
	}

}
