#include "Ember/Scripting/ScriptEngine.h"

#include "Ember/Audio/AudioEngine.h"
#include "Ember/Audio/SceneAudio.h"
#include "Ember/Core/FileSystem.h"
#include "Ember/Core/Input.h"
#include "Ember/Core/Log.h"
#include "Ember/Math/Math.h"
#include "Ember/Physics/PhysicsWorld.h"
#include "Ember/Scene/ComponentRegistry.h"
#include "Ember/Scene/Components.h"
#include "Ember/Scene/Entity.h"
#include "Ember/Scene/Scene.h"
#include "Ember/Scene/SceneSerializer.h"

#include <sol/sol.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <random>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Ember {

	namespace {

		// Script-side entity handle. Holds the UUID (not the entt id) so a handle to a destroyed entity can never
		// alias a newly created one.
		struct LuaEntity
		{
			UUID ID = 0;
			Scene* Context = nullptr;

			Entity Get() const { return Context ? Context->GetEntityByUUID(ID) : Entity(); }
		};

		std::string FormatLuaArgs(const sol::variadic_args& args, sol::state_view lua)
		{
			std::string text;
			sol::protected_function toString = lua["tostring"];
			bool first = true;
			for (const sol::stack_proxy& arg : args)
			{
				if (!first)
					text += "\t";
				first = false;
				sol::protected_function_result result = toString(arg);
				text += result.valid() ? result.get<std::string>() : std::string("?");
			}
			return text;
		}

		bool IsLuaInteger(const sol::object& object)
		{
			lua_State* L = object.lua_state();
			object.push(L);
			const bool isInteger = lua_isinteger(L, -1) != 0;
			lua_pop(L, 1);
			return isInteger;
		}

		YAML::Node LuaToYAML(const sol::object& object, int depth = 0)
		{
			if (depth > 32)
				return YAML::Node();

			switch (object.get_type())
			{
				case sol::type::boolean:
					return YAML::Node(object.as<bool>());
				case sol::type::number:
					if (IsLuaInteger(object))
						return YAML::Node(object.as<int64_t>());
					return YAML::Node(object.as<double>());
				case sol::type::string:
					return YAML::Node(object.as<std::string>());
				case sol::type::userdata:
				{
					if (object.is<glm::vec3>())
					{
						const glm::vec3 v = object.as<glm::vec3>();
						YAML::Node node;
						node.push_back(v.x); node.push_back(v.y); node.push_back(v.z);
						return node;
					}
					if (object.is<glm::vec2>())
					{
						const glm::vec2 v = object.as<glm::vec2>();
						YAML::Node node;
						node.push_back(v.x); node.push_back(v.y);
						return node;
					}
					if (object.is<LuaEntity>())
						return YAML::Node(static_cast<uint64_t>(object.as<LuaEntity>().ID));
					return YAML::Node();
				}
				case sol::type::table:
				{
					sol::table table = object.as<sol::table>();
					const size_t length = table.size();
					size_t count = 0;
					bool sequential = true;
					for (const auto& [key, value] : table)
					{
						count++;
						if (key.get_type() != sol::type::number || !IsLuaInteger(key))
							sequential = false;
					}
					if (sequential && count == length && length > 0)
					{
						YAML::Node node(YAML::NodeType::Sequence);
						for (size_t i = 1; i <= length; i++)
							node.push_back(LuaToYAML(table[i], depth + 1));
						return node;
					}
					YAML::Node node(YAML::NodeType::Map);
					for (const auto& [key, value] : table)
					{
						if (key.get_type() == sol::type::string)
							node[key.as<std::string>()] = LuaToYAML(value, depth + 1);
					}
					return node;
				}
				default:
					return YAML::Node();
			}
		}

		sol::object YAMLToLua(const YAML::Node& node, sol::state_view lua)
		{
			if (!node || node.IsNull())
				return sol::make_object(lua, sol::lua_nil);

			if (node.IsScalar())
			{
				const std::string& text = node.Scalar();
				if (text == "true")
					return sol::make_object(lua, true);
				if (text == "false")
					return sol::make_object(lua, false);
				int64_t integer = 0;
				auto [intEnd, intError] = std::from_chars(text.data(), text.data() + text.size(), integer);
				if (intError == std::errc() && intEnd == text.data() + text.size() && !text.empty())
					return sol::make_object(lua, integer);
				uint64_t unsignedInteger = 0;
				auto [uEnd, uError] = std::from_chars(text.data(), text.data() + text.size(), unsignedInteger);
				if (uError == std::errc() && uEnd == text.data() + text.size() && !text.empty())
					return sol::make_object(lua, static_cast<int64_t>(unsignedInteger));
				try
				{
					size_t consumed = 0;
					const double number = std::stod(text, &consumed);
					if (consumed == text.size())
						return sol::make_object(lua, number);
				}
				catch (const std::exception&)
				{
				}
				return sol::make_object(lua, text);
			}

			if (node.IsSequence())
			{
				bool allNumbers = node.size() > 0;
				for (const YAML::Node& child : node)
				{
					if (!child.IsScalar())
					{
						allNumbers = false;
						break;
					}
					try
					{
						size_t consumed = 0;
						std::stod(child.Scalar(), &consumed);
						allNumbers &= consumed == child.Scalar().size();
					}
					catch (const std::exception&)
					{
						allNumbers = false;
					}
				}
				if (allNumbers && node.size() == 3)
					return sol::make_object(lua, glm::vec3(node[0].as<float>(), node[1].as<float>(), node[2].as<float>()));
				if (allNumbers && node.size() == 2)
					return sol::make_object(lua, glm::vec2(node[0].as<float>(), node[1].as<float>()));

				sol::table table = lua.create_table();
				int index = 1;
				for (const YAML::Node& child : node)
					table[index++] = YAMLToLua(child, lua);
				return table;
			}

			sol::table table = lua.create_table();
			for (const auto& item : node)
				table[item.first.as<std::string>()] = YAMLToLua(item.second, lua);
			return table;
		}

	}

	// ----------------------------------------------------------------------------------------------

	struct ScriptEngine::Impl
	{
		struct Instance
		{
			std::string Path;
			sol::table Self;
			bool Created = false;
			bool Failed = false;
		};

		Scene* SceneContext = nullptr;
		sol::state Lua;
		std::unordered_map<UUID, Instance> Instances;
		std::unordered_map<std::string, sol::table> Classes;
		std::unordered_set<std::string> FailedClasses;
		std::unordered_map<std::string, sol::object> Modules;
		std::unordered_set<std::string> WarnedKeys;
		std::vector<std::string> Errors;
		std::vector<ScriptTestResult> TestResults;
		std::mt19937 Random{ 1337u };
		bool Running = false;

		void ReportError(const std::string& message)
		{
			EMBER_CORE_ERROR("[Script] {}", message);
			Errors.push_back(message);
		}

		LuaEntity Wrap(Entity entity) const { return LuaEntity{ entity ? entity.GetUUID() : UUID(0), SceneContext }; }

		sol::object WrapOrNil(Entity entity)
		{
			if (!entity)
				return sol::make_object(Lua, sol::lua_nil);
			return sol::make_object(Lua, Wrap(entity));
		}

		KeyCode ParseKey(const std::string& name)
		{
			const KeyCode key = KeyCodeFromString(name);
			if (key == KeyCode::Unknown && WarnedKeys.insert(name).second)
				ReportError("Unknown key name '" + name + "'");
			return key;
		}

		MouseButton ParseButton(const std::string& name)
		{
			bool valid = false;
			const MouseButton button = MouseButtonFromString(name, &valid);
			if (!valid && WarnedKeys.insert(name).second)
				ReportError("Unknown mouse button '" + name + "'");
			return button;
		}

		// Resolves a script-supplied asset path. Paths that could escape the asset directory are rejected.
		std::optional<std::filesystem::path> ResolveAsset(const std::string& path)
		{
			if (!FileSystem::IsSafeRelativePath(path))
			{
				ReportError("Rejected unsafe asset path '" + path + "' (must be relative, without '..')");
				return std::nullopt;
			}
			return SceneContext->GetAssetDirectory() / path;
		}

		// Loads a script file and returns its class table (cached). Returns an invalid table on failure.
		sol::table LoadClass(const std::string& path)
		{
			if (auto it = Classes.find(path); it != Classes.end())
				return it->second;
			if (FailedClasses.contains(path))
				return sol::table();

			auto fail = [&](const std::string& message)
			{
				ReportError(path + ": " + message);
				FailedClasses.insert(path);
				return sol::table();
			};

			const std::optional<std::filesystem::path> resolved = ResolveAsset(path);
			if (!resolved)
				return fail("unsafe script path");
			std::optional<std::string> source = FileSystem::ReadText(*resolved);
			if (!source)
				return fail("script file not found");

			sol::load_result chunk = Lua.load(*source, "@" + path, sol::load_mode::text);
			if (!chunk.valid())
			{
				sol::error error = chunk;
				return fail(error.what());
			}
			sol::protected_function function = chunk;
			sol::protected_function_result result = function();
			if (!result.valid())
			{
				sol::error error = result;
				return fail(error.what());
			}
			if (result.return_count() < 1 || result.get_type() != sol::type::table)
				return fail("script must return a table (e.g. 'local Script = {} ... return Script')");

			sol::table cls = result;
			Classes[path] = cls;
			return cls;
		}

		template<typename... Args>
		void Call(const UUID& id, const char* method, Args&&... args)
		{
			auto it = Instances.find(id);
			if (it == Instances.end() || it->second.Failed)
				return;
			// Copy what we need: the callback may create or destroy instances, which can rehash the map and
			// invalidate references into it.
			const std::string path = it->second.Path;
			sol::table self = it->second.Self;
			sol::object member = self[method];
			if (member.get_type() != sol::type::function)
				return;
			sol::protected_function function = member;
			sol::protected_function_result result = function(self, std::forward<Args>(args)...);
			if (!result.valid())
			{
				sol::error error = result;
				auto again = Instances.find(id);
				if (again != Instances.end())
					again->second.Failed = true;
				ReportError(path + ": error in " + method + ": " + error.what() + " (script disabled for this entity)");
			}
		}

		// Creates the instance for an entity if needed (also handles script path changes). Returns true if an
		// instance exists afterwards.
		bool EnsureInstance(Entity entity)
		{
			const auto* script = entity.TryGetComponent<ScriptComponent>();
			const UUID id = entity.GetUUID();
			auto it = Instances.find(id);
			if (!script || script->Script.empty())
			{
				if (it != Instances.end())
					DestroyInstance(id);
				return false;
			}
			if (it != Instances.end())
			{
				if (it->second.Path == script->Script)
					return !it->second.Failed;
				DestroyInstance(id);
			}

			Instance instance;
			instance.Path = script->Script;
			sol::table cls = LoadClass(script->Script);
			if (!cls.valid())
			{
				instance.Failed = true;
				Instances[id] = std::move(instance);
				return false;
			}

			instance.Self = Lua.create_table();
			instance.Self["entity"] = Wrap(entity);
			sol::table metatable = Lua.create_table();
			metatable["__index"] = cls;
			instance.Self[sol::metatable_key] = metatable;
			Instances[id] = std::move(instance);

			Instances[id].Created = true;
			Call(id, "OnCreate");
			auto created = Instances.find(id);
			return created != Instances.end() && !created->second.Failed;
		}

		void DestroyInstance(const UUID& id)
		{
			auto it = Instances.find(id);
			if (it == Instances.end())
				return;
			if (it->second.Created && !it->second.Failed)
				Call(id, "OnDestroy");
			Instances.erase(id);
		}

		std::vector<UUID> ScriptedEntitiesInOrder()
		{
			std::vector<std::pair<uint64_t, UUID>> sorted;
			auto view = SceneContext->GetAllEntitiesWith<ScriptComponent, IDComponent>();
			for (entt::entity handle : view)
			{
				const auto& id = view.get<IDComponent>(handle);
				sorted.emplace_back(id.CreationIndex, id.ID);
			}
			std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
			std::vector<UUID> result;
			result.reserve(sorted.size());
			for (const auto& entry : sorted)
				result.push_back(entry.second);
			return result;
		}

		void SyncInstances()
		{
			// Remove instances whose entity vanished or lost its script.
			std::vector<UUID> stale;
			for (const auto& [id, instance] : Instances)
			{
				Entity entity = SceneContext->GetEntityByUUID(id);
				if (!entity || !entity.HasComponent<ScriptComponent>())
					stale.push_back(id);
			}
			for (const UUID& id : stale)
				DestroyInstance(id);

			for (const UUID& id : ScriptedEntitiesInOrder())
			{
				if (Entity entity = SceneContext->GetEntityByUUID(id))
					EnsureInstance(entity);
			}
		}

		void RegisterAPI();
		void RegisterMath();
		void RegisterEntity();
		void RegisterGlobals();
	};

	// ----------------------------------------------------------------------------------------------

	void ScriptEngine::Impl::RegisterMath()
	{
		Lua.new_usertype<glm::vec2>("vec2",
			sol::call_constructor, sol::constructors<glm::vec2(), glm::vec2(float), glm::vec2(float, float)>(),
			"x", &glm::vec2::x,
			"y", &glm::vec2::y,
			"length", [](const glm::vec2& v) { return glm::length(v); },
			"normalized", [](const glm::vec2& v) { return glm::length(v) > 0.0f ? glm::normalize(v) : v; },
			"dot", [](const glm::vec2& a, const glm::vec2& b) { return glm::dot(a, b); },
			sol::meta_function::addition, [](const glm::vec2& a, const glm::vec2& b) { return a + b; },
			sol::meta_function::subtraction, [](const glm::vec2& a, const glm::vec2& b) { return a - b; },
			sol::meta_function::multiplication, sol::overload(
				[](const glm::vec2& a, const glm::vec2& b) { return a * b; },
				[](const glm::vec2& a, float s) { return a * s; },
				[](float s, const glm::vec2& a) { return a * s; }),
			sol::meta_function::division, sol::overload(
				[](const glm::vec2& a, const glm::vec2& b) { return a / b; },
				[](const glm::vec2& a, float s) { return a / s; }),
			sol::meta_function::unary_minus, [](const glm::vec2& a) { return -a; },
			sol::meta_function::equal_to, [](const glm::vec2& a, const glm::vec2& b) { return a == b; },
			sol::meta_function::to_string, [](const glm::vec2& v)
			{
				std::ostringstream stream;
				stream << "vec2(" << v.x << ", " << v.y << ")";
				return stream.str();
			});

		Lua.new_usertype<glm::vec3>("vec3",
			sol::call_constructor, sol::constructors<glm::vec3(), glm::vec3(float), glm::vec3(float, float, float)>(),
			"x", &glm::vec3::x,
			"y", &glm::vec3::y,
			"z", &glm::vec3::z,
			"length", [](const glm::vec3& v) { return glm::length(v); },
			"normalized", [](const glm::vec3& v) { return glm::length(v) > 0.0f ? glm::normalize(v) : v; },
			"dot", [](const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, b); },
			"cross", [](const glm::vec3& a, const glm::vec3& b) { return glm::cross(a, b); },
			"distance", [](const glm::vec3& a, const glm::vec3& b) { return glm::distance(a, b); },
			"lerp", [](const glm::vec3& a, const glm::vec3& b, float t) { return glm::mix(a, b, t); },
			sol::meta_function::addition, [](const glm::vec3& a, const glm::vec3& b) { return a + b; },
			sol::meta_function::subtraction, [](const glm::vec3& a, const glm::vec3& b) { return a - b; },
			sol::meta_function::multiplication, sol::overload(
				[](const glm::vec3& a, const glm::vec3& b) { return a * b; },
				[](const glm::vec3& a, float s) { return a * s; },
				[](float s, const glm::vec3& a) { return a * s; }),
			sol::meta_function::division, sol::overload(
				[](const glm::vec3& a, const glm::vec3& b) { return a / b; },
				[](const glm::vec3& a, float s) { return a / s; }),
			sol::meta_function::unary_minus, [](const glm::vec3& a) { return -a; },
			sol::meta_function::equal_to, [](const glm::vec3& a, const glm::vec3& b) { return a == b; },
			sol::meta_function::to_string, [](const glm::vec3& v)
			{
				std::ostringstream stream;
				stream << "vec3(" << v.x << ", " << v.y << ", " << v.z << ")";
				return stream.str();
			});

		sol::table math = Lua.create_named_table("Math");
		math["Lerp"] = [](float a, float b, float t) { return a + (b - a) * t; };
		math["Clamp"] = [](float value, float lo, float hi) { return std::clamp(value, std::min(lo, hi), std::max(lo, hi)); };
		math["Radians"] = [](float degrees) { return glm::radians(degrees); };
		math["Degrees"] = [](float radians) { return glm::degrees(radians); };
		math["Random"] = [this](sol::optional<float> lo, sol::optional<float> hi)
		{
			std::uniform_real_distribution<float> distribution(lo.value_or(0.0f), hi.value_or(1.0f));
			return distribution(Random);
		};
		math["RandomInt"] = [this](int lo, int hi)
		{
			std::uniform_int_distribution<int> distribution(std::min(lo, hi), std::max(lo, hi));
			return distribution(Random);
		};
		math["Seed"] = [this](uint32_t seed) { Random.seed(seed); };
	}

	void ScriptEngine::Impl::RegisterEntity()
	{
		auto get = [](const LuaEntity& self) { return self.Get(); };
		auto requireTransform = [](const LuaEntity& self) -> TransformComponent*
		{
			Entity entity = self.Get();
			return entity ? &entity.GetComponent<TransformComponent>() : nullptr;
		};

		sol::usertype<LuaEntity> type = Lua.new_usertype<LuaEntity>("Entity", sol::no_constructor);

		type["id"] = sol::readonly_property([](const LuaEntity& self) { return static_cast<int64_t>(static_cast<uint64_t>(self.ID)); });
		type["name"] = sol::property(
			[get](const LuaEntity& self) { Entity e = get(self); return e ? e.GetName() : std::string(); },
			[get](const LuaEntity& self, const std::string& name) { if (Entity e = get(self)) e.GetComponent<TagComponent>().Tag = name; });
		type["IsValid"] = [get](const LuaEntity& self) { return static_cast<bool>(get(self)); };
		type[sol::meta_function::equal_to] = [](const LuaEntity& a, const LuaEntity& b) { return a.ID == b.ID && a.Context == b.Context; };
		type[sol::meta_function::to_string] = [get](const LuaEntity& self)
		{
			Entity e = get(self);
			return "Entity(" + (e ? e.GetName() : std::string("<destroyed>")) + ")";
		};

		// --- Transform (local) ---------------------------------------------------------------------
		type["GetPosition"] = [requireTransform](const LuaEntity& self) { auto* t = requireTransform(self); return t ? t->Translation : glm::vec3(0.0f); };
		type["SetPosition"] = [requireTransform](const LuaEntity& self, const glm::vec3& v) { if (auto* t = requireTransform(self)) t->Translation = v; };
		type["Translate"] = [requireTransform](const LuaEntity& self, const glm::vec3& v) { if (auto* t = requireTransform(self)) t->Translation += v; };
		type["GetRotation"] = [requireTransform](const LuaEntity& self) { auto* t = requireTransform(self); return t ? t->Rotation : glm::vec3(0.0f); };
		type["SetRotation"] = [requireTransform](const LuaEntity& self, const glm::vec3& v) { if (auto* t = requireTransform(self)) t->Rotation = v; };
		type["Rotate"] = [requireTransform](const LuaEntity& self, const glm::vec3& v) { if (auto* t = requireTransform(self)) t->Rotation += v; };
		type["GetRotationDegrees"] = [requireTransform](const LuaEntity& self) { auto* t = requireTransform(self); return t ? glm::degrees(t->Rotation) : glm::vec3(0.0f); };
		type["SetRotationDegrees"] = [requireTransform](const LuaEntity& self, const glm::vec3& v) { if (auto* t = requireTransform(self)) t->Rotation = glm::radians(v); };
		type["GetScale"] = [requireTransform](const LuaEntity& self) { auto* t = requireTransform(self); return t ? t->Scale : glm::vec3(1.0f); };
		type["SetScale"] = [requireTransform](const LuaEntity& self, const glm::vec3& v) { if (auto* t = requireTransform(self)) t->Scale = v; };

		// --- Transform (world) ---------------------------------------------------------------------
		type["GetWorldPosition"] = [get](const LuaEntity& self)
		{
			Entity e = get(self);
			return e ? glm::vec3(e.GetScene()->GetWorldTransform(e)[3]) : glm::vec3(0.0f);
		};
		type["SetWorldPosition"] = [get](const LuaEntity& self, const glm::vec3& position)
		{
			Entity e = get(self);
			if (!e)
				return;
			glm::mat4 world = e.GetScene()->GetWorldTransform(e);
			world[3] = glm::vec4(position, 1.0f);
			e.GetScene()->SetWorldTransform(e, world);
		};
		auto axis = [get](const LuaEntity& self, int column, float sign)
		{
			Entity e = get(self);
			if (!e)
				return glm::vec3(0.0f);
			const glm::vec3 direction = glm::vec3(e.GetScene()->GetWorldTransform(e)[column]) * sign;
			return glm::length(direction) > 0.0f ? glm::normalize(direction) : direction;
		};
		type["GetForward"] = [axis](const LuaEntity& self) { return axis(self, 2, -1.0f); };
		type["GetRight"] = [axis](const LuaEntity& self) { return axis(self, 0, 1.0f); };
		type["GetUp"] = [axis](const LuaEntity& self) { return axis(self, 1, 1.0f); };
		type["LookAt"] = [get](const LuaEntity& self, const glm::vec3& target, sol::optional<glm::vec3> upOpt)
		{
			Entity e = get(self);
			if (!e)
				return;
			glm::vec3 position, scale;
			glm::quat rotation;
			Math::DecomposeTransform(e.GetScene()->GetWorldTransform(e), position, rotation, scale);
			const glm::vec3 direction = target - position;
			if (glm::length(direction) < 1e-6f)
				return;
			glm::vec3 up = upOpt.value_or(glm::vec3(0.0f, 1.0f, 0.0f));
			if (std::abs(glm::dot(glm::normalize(direction), glm::normalize(up))) > 0.999f)
				up = glm::vec3(0.0f, 0.0f, 1.0f);
			rotation = glm::quatLookAt(glm::normalize(direction), up);
			e.GetScene()->SetWorldTransform(e, Math::ComposeTransform(position, rotation, scale));
		};

		// --- Hierarchy -----------------------------------------------------------------------------
		type["GetParent"] = [this, get](const LuaEntity& self)
		{
			Entity e = get(self);
			return e ? WrapOrNil(e.GetScene()->GetParent(e)) : sol::make_object(Lua, sol::lua_nil);
		};
		type["SetParent"] = [get](const LuaEntity& self, sol::optional<LuaEntity> parent)
		{
			Entity e = get(self);
			if (!e)
				return false;
			return e.GetScene()->SetParent(e, parent ? parent->Get() : Entity(), true);
		};
		type["GetChildren"] = [this, get](const LuaEntity& self)
		{
			sol::table children = Lua.create_table();
			if (Entity e = get(self))
			{
				int index = 1;
				for (Entity child : e.GetScene()->GetChildren(e))
					children[index++] = Wrap(child);
			}
			return children;
		};
		type["Destroy"] = [get](const LuaEntity& self)
		{
			if (Entity e = get(self))
				e.GetScene()->DestroyEntity(e);
		};

		// --- Generic component access (every registered component, via ComponentRegistry) -------------
		type["HasComponent"] = [get](const LuaEntity& self, const std::string& name)
		{
			Entity e = get(self);
			const ComponentInfo* info = ComponentRegistry::Find(name);
			return e && info && info->Has(e);
		};
		type["GetComponent"] = [this, get](const LuaEntity& self, const std::string& name) -> sol::object
		{
			Entity e = get(self);
			const ComponentInfo* info = ComponentRegistry::Find(name);
			if (!e || !info || !info->Has(e))
				return sol::make_object(Lua, sol::lua_nil);
			return YAMLToLua(info->Serialize(e), Lua);
		};
		auto setComponent = [this, get](const LuaEntity& self, const std::string& name, sol::optional<sol::table> values)
		{
			Entity e = get(self);
			const ComponentInfo* info = ComponentRegistry::Find(name);
			if (!e)
				return false;
			if (!info)
			{
				ReportError("Unknown component '" + name + "'");
				return false;
			}
			if (info->Name == "Relationship")
			{
				ReportError("Relationship cannot be set directly; use SetParent");
				return false;
			}
			const YAML::Node patch = values ? LuaToYAML(sol::object(*values)) : YAML::Node(YAML::NodeType::Map);
			if (!info->Deserialize(e, patch))
			{
				ReportError("Invalid values for component '" + name + "' on entity '" + e.GetName() + "'");
				return false;
			}
			return true;
		};
		type["AddComponent"] = setComponent;
		type["SetComponent"] = setComponent;
		type["RemoveComponent"] = [this, get](const LuaEntity& self, const std::string& name)
		{
			Entity e = get(self);
			const ComponentInfo* info = ComponentRegistry::Find(name);
			if (!e || !info)
				return false;
			if (!info->UserAddable)
			{
				ReportError("Component '" + name + "' cannot be removed");
				return false;
			}
			info->Remove(e);
			return true;
		};

		// --- Rendering shortcuts ---------------------------------------------------------------------
		type["SetColor"] = [get](const LuaEntity& self, float r, float g, float b, sol::optional<float> a)
		{
			Entity e = get(self);
			if (!e)
				return;
			auto& renderer = e.HasComponent<MeshRendererComponent>() ? e.GetComponent<MeshRendererComponent>() : e.AddComponent<MeshRendererComponent>();
			renderer.OverrideMaterial = true;
			renderer.MaterialOverride.BaseColor = glm::vec4(r, g, b, a.value_or(1.0f));
		};
		type["SetVisible"] = [get](const LuaEntity& self, bool visible)
		{
			if (Entity e = get(self); e && e.HasComponent<MeshRendererComponent>())
				e.GetComponent<MeshRendererComponent>().Visible = visible;
		};

		// --- Physics -------------------------------------------------------------------------------
		auto physics = [](const LuaEntity& self) -> PhysicsWorld* { return self.Context ? self.Context->GetPhysicsWorld() : nullptr; };
		type["AddForce"] = [get, physics](const LuaEntity& self, const glm::vec3& v) { if (auto* p = physics(self)) p->AddForce(get(self), v); };
		type["AddImpulse"] = [get, physics](const LuaEntity& self, const glm::vec3& v) { if (auto* p = physics(self)) p->AddImpulse(get(self), v); };
		type["AddTorque"] = [get, physics](const LuaEntity& self, const glm::vec3& v) { if (auto* p = physics(self)) p->AddTorque(get(self), v); };
		type["SetVelocity"] = [get, physics](const LuaEntity& self, const glm::vec3& v) { if (auto* p = physics(self)) p->SetLinearVelocity(get(self), v); };
		type["GetVelocity"] = [get, physics](const LuaEntity& self) { auto* p = physics(self); return p ? p->GetLinearVelocity(get(self)) : glm::vec3(0.0f); };
		type["SetAngularVelocity"] = [get, physics](const LuaEntity& self, const glm::vec3& v) { if (auto* p = physics(self)) p->SetAngularVelocity(get(self), v); };
		type["GetAngularVelocity"] = [get, physics](const LuaEntity& self) { auto* p = physics(self); return p ? p->GetAngularVelocity(get(self)) : glm::vec3(0.0f); };
		type["RefreshPhysics"] = [get, physics](const LuaEntity& self) { if (auto* p = physics(self)) p->RecreateBody(get(self)); };

		// --- Audio ---------------------------------------------------------------------------------
		auto audio = [](const LuaEntity& self) -> SceneAudio* { return self.Context ? self.Context->GetAudio() : nullptr; };
		type["PlaySound"] = [get, audio](const LuaEntity& self) { auto* a = audio(self); return a && a->Play(get(self)); };
		type["StopSound"] = [get, audio](const LuaEntity& self) { if (auto* a = audio(self)) a->Stop(get(self)); };
		type["IsSoundPlaying"] = [get, audio](const LuaEntity& self) { auto* a = audio(self); return a && a->IsPlaying(get(self)); };

		// --- Scripts -------------------------------------------------------------------------------
		type["GetScript"] = [this, get](const LuaEntity& self) -> sol::object
		{
			Entity e = get(self);
			if (!e || !EnsureInstance(e))
				return sol::make_object(Lua, sol::lua_nil);
			return Instances[e.GetUUID()].Self;
		};
	}

	void ScriptEngine::Impl::RegisterGlobals()
	{
		// --- Log -----------------------------------------------------------------------------------
		sol::table log = Lua.create_named_table("Log");
		log["Info"] = [this](sol::variadic_args args) { EMBER_INFO("[Lua] {}", FormatLuaArgs(args, Lua)); };
		log["Warn"] = [this](sol::variadic_args args) { EMBER_WARN("[Lua] {}", FormatLuaArgs(args, Lua)); };
		log["Error"] = [this](sol::variadic_args args) { EMBER_ERROR("[Lua] {}", FormatLuaArgs(args, Lua)); };
		Lua["print"] = [this](sol::variadic_args args) { EMBER_INFO("[Lua] {}", FormatLuaArgs(args, Lua)); };

		// --- Input ---------------------------------------------------------------------------------
		sol::table input = Lua.create_named_table("Input");
		input["IsKeyDown"] = [this](const std::string& key) { return Input::IsKeyPressed(ParseKey(key)); };
		input["IsKeyPressed"] = [this](const std::string& key) { return Input::IsKeyJustPressed(ParseKey(key)); };
		input["IsKeyReleased"] = [this](const std::string& key) { return Input::IsKeyJustReleased(ParseKey(key)); };
		input["IsMouseButtonDown"] = [this](const std::string& button) { return Input::IsMouseButtonPressed(ParseButton(button)); };
		input["IsMouseButtonPressed"] = [this](const std::string& button) { return Input::IsMouseButtonJustPressed(ParseButton(button)); };
		input["IsMouseButtonReleased"] = [this](const std::string& button) { return Input::IsMouseButtonJustReleased(ParseButton(button)); };
		input["GetMousePosition"] = []() { return Input::GetMousePosition(); };
		input["GetMouseDelta"] = []() { return Input::GetMouseDelta(); };
		input["GetScrollDelta"] = []() { return Input::GetScrollDelta(); };

		// --- Scene ---------------------------------------------------------------------------------
		sol::table scene = Lua.create_named_table("Scene");
		scene["CreateEntity"] = [this](sol::optional<std::string> name) { return Wrap(SceneContext->CreateEntity(name.value_or("Entity"))); };
		scene["FindEntityByName"] = [this](const std::string& name) { return WrapOrNil(SceneContext->FindEntityByName(name)); };
		scene["GetEntityByID"] = [this](int64_t id) { return WrapOrNil(SceneContext->GetEntityByUUID(static_cast<uint64_t>(id))); };
		scene["Destroy"] = [](const LuaEntity& entity)
		{
			if (Entity e = entity.Get())
				e.GetScene()->DestroyEntity(e);
		};
		scene["Instantiate"] = [this](const std::string& prefab, sol::optional<glm::vec3> position, sol::optional<LuaEntity> parent) -> sol::object
		{
			const std::optional<std::filesystem::path> resolved = ResolveAsset(prefab);
			if (!resolved)
				return sol::make_object(Lua, sol::lua_nil);
			std::string error;
			Entity root = SceneSerializer::InstantiatePrefab(*SceneContext, *resolved, prefab, &error);
			if (!root)
			{
				ReportError("Scene.Instantiate('" + prefab + "') failed: " + error);
				return sol::make_object(Lua, sol::lua_nil);
			}
			if (parent)
				SceneContext->SetParent(root, parent->Get(), false);
			if (position)
				root.GetComponent<TransformComponent>().Translation = *position;
			return sol::make_object(Lua, Wrap(root));
		};
		scene["GetEntities"] = [this]()
		{
			sol::table result = Lua.create_table();
			int index = 1;
			for (Entity entity : SceneContext->GetAllEntitiesOrdered())
				result[index++] = Wrap(entity);
			return result;
		};
		scene["GetEntitiesWith"] = [this](const std::string& componentName)
		{
			sol::table result = Lua.create_table();
			const ComponentInfo* info = ComponentRegistry::Find(componentName);
			if (!info)
			{
				ReportError("Unknown component '" + componentName + "'");
				return result;
			}
			int index = 1;
			for (Entity entity : SceneContext->GetAllEntitiesOrdered())
			{
				if (info->Has(entity))
					result[index++] = Wrap(entity);
			}
			return result;
		};
		scene["GetPrimaryCamera"] = [this]() { return WrapOrNil(SceneContext->GetPrimaryCameraEntity()); };
		scene["GetTime"] = [this]() { return SceneContext->GetRuntimeTime(); };
		scene["GetEntityCount"] = [this]() { return SceneContext->GetEntityCount(); };
		scene["Raycast"] = [this](const glm::vec3& origin, const glm::vec3& direction, sol::optional<float> maxDistance) -> sol::object
		{
			PhysicsWorld* physics = SceneContext->GetPhysicsWorld();
			if (!physics)
				return sol::make_object(Lua, sol::lua_nil);
			std::optional<RaycastHit> hit = physics->Raycast(origin, direction, maxDistance.value_or(1000.0f));
			if (!hit)
				return sol::make_object(Lua, sol::lua_nil);
			sol::table result = Lua.create_table();
			result["entity"] = WrapOrNil(SceneContext->GetEntityByUUID(hit->EntityID));
			result["point"] = hit->Point;
			result["normal"] = hit->Normal;
			result["distance"] = hit->Distance;
			return result;
		};
		scene["SetGravity"] = [this](const glm::vec3& gravity) { if (auto* p = SceneContext->GetPhysicsWorld()) p->SetGravity(gravity); };
		scene["GetGravity"] = [this]() { auto* p = SceneContext->GetPhysicsWorld(); return p ? p->GetGravity() : glm::vec3(0.0f, -9.81f, 0.0f); };

		// --- Audio ---------------------------------------------------------------------------------
		sol::table audio = Lua.create_named_table("Audio");
		audio["PlayOneShot"] = [this](const std::string& clip, sol::optional<float> volume, sol::optional<glm::vec3> position)
		{
			SceneAudio* sceneAudio = SceneContext->GetAudio();
			if (!sceneAudio || !ResolveAsset(clip))
				return false;
			std::optional<glm::vec3> where;
			if (position)
				where = *position;
			return sceneAudio->PlayOneShot(clip, volume.value_or(1.0f), where);
		};
		audio["SetMasterVolume"] = [](float volume) { AudioEngine::SetMasterVolume(volume); };
		audio["GetMasterVolume"] = []() { return AudioEngine::GetMasterVolume(); };

		// --- Test (used by the feature test scene and automated test runs) ---------------------------
		sol::table test = Lua.create_named_table("Test");
		test["Check"] = [this](bool condition, const std::string& name, sol::optional<std::string> message)
		{
			TestResults.push_back({ name, condition, condition ? std::string() : message.value_or("check failed") });
			if (!condition)
				EMBER_CORE_ERROR("[Test] FAILED: {} - {}", name, message.value_or("check failed"));
			return condition;
		};
		test["Pass"] = [this](const std::string& name) { TestResults.push_back({ name, true, {} }); };
		test["Fail"] = [this](const std::string& name, sol::optional<std::string> message)
		{
			TestResults.push_back({ name, false, message.value_or("failed") });
			EMBER_CORE_ERROR("[Test] FAILED: {} - {}", name, message.value_or("failed"));
		};

		// --- require: modules from the asset directory -----------------------------------------------
		Lua["require"] = [this](const std::string& name) -> sol::object
		{
			if (auto it = Modules.find(name); it != Modules.end())
				return it->second;
			// Module names are dotted identifiers ("Scripts.Util" -> Scripts/Util.lua). Validate before mapping
			// dots to directory separators so names can never form paths outside the asset directory.
			const bool validName = !name.empty() && name.front() != '.' && name.back() != '.' &&
				name.find("..") == std::string::npos &&
				std::all_of(name.begin(), name.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.'; });
			if (!validName)
				throw sol::error("require: invalid module name '" + name + "' (expected e.g. 'Scripts.Util')");
			std::string relative = name;
			std::replace(relative.begin(), relative.end(), '.', '/');
			relative += ".lua";

			std::optional<std::string> source = FileSystem::ReadText(SceneContext->GetAssetDirectory() / relative);
			if (!source)
				throw sol::error("require: module '" + name + "' not found (" + relative + ")");
			sol::load_result chunk = Lua.load(*source, "@" + relative, sol::load_mode::text);
			if (!chunk.valid())
			{
				sol::error error = chunk;
				throw error;
			}
			sol::protected_function function = chunk;
			sol::protected_function_result result = function();
			if (!result.valid())
			{
				sol::error error = result;
				throw error;
			}
			sol::object value = result.return_count() > 0 ? result.get<sol::object>() : sol::make_object(Lua, true);
			Modules[name] = value;
			return value;
		};
	}

	void ScriptEngine::Impl::RegisterAPI()
	{
		Lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::utf8,
			sol::lib::coroutine, sol::lib::os, sol::lib::debug);

		// Sandbox: keep only safe functions.
		sol::table safeOs = Lua.create_table();
		safeOs["clock"] = Lua["os"]["clock"];
		safeOs["time"] = Lua["os"]["time"];
		safeOs["date"] = Lua["os"]["date"];
		safeOs["difftime"] = Lua["os"]["difftime"];
		Lua["os"] = safeOs;
		sol::table safeDebug = Lua.create_table();
		safeDebug["traceback"] = Lua["debug"]["traceback"];
		Lua["debug"] = safeDebug;
		Lua["dofile"] = sol::lua_nil;
		Lua["loadfile"] = sol::lua_nil;
		Lua["load"] = sol::lua_nil;
		Lua["loadstring"] = sol::lua_nil;

		// Errors carry a stack traceback.
		sol::protected_function::set_default_handler(safeDebug["traceback"]);
		// C++ exceptions thrown from bindings (e.g. require errors) become Lua errors without printing to stderr.
		Lua.set_exception_handler([](lua_State* L, sol::optional<const std::exception&>, sol::string_view description)
		{
			return sol::stack::push(L, description);
		});

		RegisterMath();
		RegisterEntity();
		RegisterGlobals();
	}

	// ----------------------------------------------------------------------------------------------

	ScriptEngine::ScriptEngine(Scene* scene)
		: m_Impl(CreateScope<Impl>())
	{
		m_Impl->SceneContext = scene;
		m_Impl->RegisterAPI();
	}

	ScriptEngine::~ScriptEngine()
	{
		OnRuntimeStop();
		// Release all Lua references before the state is destroyed.
		m_Impl->Instances.clear();
		m_Impl->Classes.clear();
		m_Impl->Modules.clear();
	}

	void ScriptEngine::OnRuntimeStart()
	{
		m_Impl->Running = true;
		m_Impl->SyncInstances();
	}

	void ScriptEngine::OnRuntimeStop()
	{
		if (!m_Impl->Running)
			return;
		m_Impl->Running = false;
		for (const UUID& id : m_Impl->ScriptedEntitiesInOrder())
			m_Impl->DestroyInstance(id);
		// Any remaining (e.g. entity lost its component this frame).
		while (!m_Impl->Instances.empty())
			m_Impl->DestroyInstance(m_Impl->Instances.begin()->first);
	}

	void ScriptEngine::OnUpdate(Timestep ts)
	{
		m_Impl->SyncInstances();
		for (const UUID& id : m_Impl->ScriptedEntitiesInOrder())
			m_Impl->Call(id, "OnUpdate", ts.GetSeconds());
	}

	void ScriptEngine::OnLateUpdate(Timestep ts)
	{
		for (const UUID& id : m_Impl->ScriptedEntitiesInOrder())
			m_Impl->Call(id, "OnLateUpdate", ts.GetSeconds());
	}

	void ScriptEngine::DispatchPhysicsEvents()
	{
		PhysicsWorld* physics = m_Impl->SceneContext->GetPhysicsWorld();
		if (!physics)
			return;
		for (const ContactEvent& event : physics->ConsumeContactEvents())
		{
			const char* method = event.IsTrigger ? (event.Begin ? "OnTriggerBegin" : "OnTriggerEnd")
				: (event.Begin ? "OnCollisionBegin" : "OnCollisionEnd");
			const LuaEntity a{ event.EntityA, m_Impl->SceneContext };
			const LuaEntity b{ event.EntityB, m_Impl->SceneContext };
			// Skip pairs involving entities that no longer exist (except for end events, which are still useful).
			if (event.Begin && (!a.Get() || !b.Get()))
				continue;
			m_Impl->Call(event.EntityA, method, b);
			m_Impl->Call(event.EntityB, method, a);
		}
	}

	void ScriptEngine::OnEntityDestroyed(Entity entity)
	{
		if (entity)
			m_Impl->DestroyInstance(entity.GetUUID());
	}

	bool ScriptEngine::ExecuteString(const std::string& code, std::string* outResult, std::string* outError)
	{
		sol::state& lua = m_Impl->Lua;
		// REPL convenience: try as an expression first.
		sol::load_result chunk = lua.load("return " + code, "=console", sol::load_mode::text);
		if (!chunk.valid())
			chunk = lua.load(code, "=console", sol::load_mode::text);
		if (!chunk.valid())
		{
			sol::error error = chunk;
			if (outError)
				*outError = error.what();
			return false;
		}

		sol::protected_function function = chunk;
		sol::protected_function_result result = function();
		if (!result.valid())
		{
			sol::error error = result;
			if (outError)
				*outError = error.what();
			return false;
		}

		if (outResult)
		{
			outResult->clear();
			sol::protected_function toString = lua["tostring"];
			for (int i = 0; i < result.return_count(); i++)
			{
				if (i > 0)
					*outResult += "\t";
				sol::protected_function_result text = toString(result.get<sol::object>(i));
				*outResult += text.valid() ? text.get<std::string>() : std::string("?");
			}
		}
		return true;
	}

	uint32_t ScriptEngine::GetErrorCount() const
	{
		return static_cast<uint32_t>(m_Impl->Errors.size());
	}

	const std::vector<std::string>& ScriptEngine::GetErrors() const
	{
		return m_Impl->Errors;
	}

	const std::vector<ScriptTestResult>& ScriptEngine::GetTestResults() const
	{
		return m_Impl->TestResults;
	}

	bool ScriptEngine::HasInstance(Entity entity) const
	{
		if (!entity)
			return false;
		auto it = m_Impl->Instances.find(entity.GetUUID());
		return it != m_Impl->Instances.end() && !it->second.Failed;
	}

}
