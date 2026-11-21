#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include <stdexcept>
#include <string>

namespace Ember {

	using Json = nlohmann::json;

	// Error raised by editor commands; becomes {"error": {"message": ...}} in the AI protocol.
	class CommandError : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	namespace JsonUtils {

		// Converts between the component data format (YAML nodes, see ComponentRegistry) and JSON.
		// Scalars become booleans or numbers when they parse as such, strings otherwise.
		Json FromYAML(const YAML::Node& node);
		YAML::Node ToYAML(const Json& json);

		// Parameter helpers: throw CommandError with a descriptive message on missing / mistyped values.
		const Json& Require(const Json& params, const char* key);
		std::string RequireString(const Json& params, const char* key);
		std::string GetString(const Json& params, const char* key, const std::string& fallback = {});
		double GetNumber(const Json& params, const char* key, double fallback);
		bool GetBool(const Json& params, const char* key, bool fallback);
		uint64_t RequireEntityID(const Json& params, const char* key = "entity");
		glm::vec3 ToVec3(const Json& value, const char* what);
		Json FromVec3(const glm::vec3& v);

	}

}
