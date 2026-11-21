#include "JsonUtils.h"

#include <charconv>
#include <cstdlib>

namespace Ember::JsonUtils {

	Json FromYAML(const YAML::Node& node)
	{
		if (!node || node.IsNull())
			return nullptr;
		if (node.IsScalar())
		{
			const std::string& text = node.Scalar();
			if (text == "true")
				return true;
			if (text == "false")
				return false;
			int64_t integer = 0;
			auto [intEnd, intError] = std::from_chars(text.data(), text.data() + text.size(), integer);
			if (!text.empty() && intError == std::errc() && intEnd == text.data() + text.size())
				return integer;
			uint64_t unsignedInteger = 0;
			auto [uEnd, uError] = std::from_chars(text.data(), text.data() + text.size(), unsignedInteger);
			if (!text.empty() && uError == std::errc() && uEnd == text.data() + text.size())
				return unsignedInteger;
			char* end = nullptr;
			const double number = std::strtod(text.c_str(), &end);
			if (!text.empty() && end == text.c_str() + text.size())
				return number;
			return text;
		}
		if (node.IsSequence())
		{
			Json array = Json::array();
			for (const YAML::Node& child : node)
				array.push_back(FromYAML(child));
			return array;
		}
		Json object = Json::object();
		for (const auto& item : node)
			object[item.first.as<std::string>()] = FromYAML(item.second);
		return object;
	}

	YAML::Node ToYAML(const Json& json)
	{
		switch (json.type())
		{
			case Json::value_t::null:
				return YAML::Node();
			case Json::value_t::boolean:
				return YAML::Node(json.get<bool>());
			case Json::value_t::number_integer:
				return YAML::Node(json.get<int64_t>());
			case Json::value_t::number_unsigned:
				return YAML::Node(json.get<uint64_t>());
			case Json::value_t::number_float:
				return YAML::Node(json.get<double>());
			case Json::value_t::string:
				return YAML::Node(json.get<std::string>());
			case Json::value_t::array:
			{
				YAML::Node node(YAML::NodeType::Sequence);
				for (const Json& item : json)
					node.push_back(ToYAML(item));
				return node;
			}
			case Json::value_t::object:
			{
				YAML::Node node(YAML::NodeType::Map);
				for (auto it = json.begin(); it != json.end(); ++it)
					node[it.key()] = ToYAML(it.value());
				return node;
			}
			default:
				return YAML::Node();
		}
	}

	const Json& Require(const Json& params, const char* key)
	{
		if (!params.is_object() || !params.contains(key))
			throw CommandError(std::string("missing parameter '") + key + "'");
		return params.at(key);
	}

	std::string RequireString(const Json& params, const char* key)
	{
		const Json& value = Require(params, key);
		if (!value.is_string())
			throw CommandError(std::string("parameter '") + key + "' must be a string");
		return value.get<std::string>();
	}

	std::string GetString(const Json& params, const char* key, const std::string& fallback)
	{
		if (!params.is_object() || !params.contains(key) || params.at(key).is_null())
			return fallback;
		if (!params.at(key).is_string())
			throw CommandError(std::string("parameter '") + key + "' must be a string");
		return params.at(key).get<std::string>();
	}

	double GetNumber(const Json& params, const char* key, double fallback)
	{
		if (!params.is_object() || !params.contains(key) || params.at(key).is_null())
			return fallback;
		if (!params.at(key).is_number())
			throw CommandError(std::string("parameter '") + key + "' must be a number");
		return params.at(key).get<double>();
	}

	bool GetBool(const Json& params, const char* key, bool fallback)
	{
		if (!params.is_object() || !params.contains(key) || params.at(key).is_null())
			return fallback;
		if (!params.at(key).is_boolean())
			throw CommandError(std::string("parameter '") + key + "' must be a boolean");
		return params.at(key).get<bool>();
	}

	uint64_t RequireEntityID(const Json& params, const char* key)
	{
		const Json& value = Require(params, key);
		if (value.is_number_unsigned())
			return value.get<uint64_t>();
		if (value.is_number_integer())
			return static_cast<uint64_t>(value.get<int64_t>());
		if (value.is_string())
		{
			// IDs may be passed as strings (some JSON clients lose precision on 64-bit integers).
			const std::string text = value.get<std::string>();
			uint64_t id = 0;
			auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), id);
			if (error == std::errc() && end == text.data() + text.size())
				return id;
		}
		throw CommandError(std::string("parameter '") + key + "' must be an entity id (integer or numeric string)");
	}

	glm::vec3 ToVec3(const Json& value, const char* what)
	{
		if (!value.is_array() || value.size() != 3 || !value[0].is_number() || !value[1].is_number() || !value[2].is_number())
			throw CommandError(std::string(what) + " must be an array of 3 numbers");
		return { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
	}

	Json FromVec3(const glm::vec3& v)
	{
		return Json::array({ v.x, v.y, v.z });
	}

}
