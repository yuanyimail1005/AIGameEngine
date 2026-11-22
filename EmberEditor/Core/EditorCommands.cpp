#include "EditorCommands.h"

#include "EditorContext.h"
#include "GameExporter.h"

#include "Ember/Core/FileSystem.h"
#include "Ember/Core/Input.h"
#include "Ember/Core/Log.h"
#include "Ember/Physics/PhysicsWorld.h"
#include "Ember/Renderer/GraphicsDevice.h"
#include "Ember/Renderer/MaterialSerializer.h"
#include "Ember/Scene/ComponentRegistry.h"
#include "Ember/Scene/SceneSerializer.h"
#include "Ember/Scripting/ScriptEngine.h"

#include <algorithm>
#include <charconv>

namespace Ember {

	using namespace JsonUtils;

	namespace {

		std::string IdString(UUID id) { return std::to_string(static_cast<uint64_t>(id)); }

		Json EntitySummary(Scene& scene, Entity entity)
		{
			Json children = Json::array();
			for (Entity child : scene.GetChildren(entity))
				children.push_back(IdString(child.GetUUID()));
			Json components = Json::array();
			for (const ComponentInfo& info : ComponentRegistry::GetAll())
			{
				if (info.Name != "Tag" && info.Name != "Relationship" && info.Has(entity))
					components.push_back(info.Name);
			}
			Entity parent = scene.GetParent(entity);
			return Json{
				{ "id", IdString(entity.GetUUID()) },
				{ "name", entity.GetName() },
				{ "parent", parent ? Json(IdString(parent.GetUUID())) : Json(nullptr) },
				{ "children", children },
				{ "components", components },
			};
		}

		Json EntityDetails(Scene& scene, Entity entity)
		{
			Json result = EntitySummary(scene, entity);
			result["data"] = FromYAML(ComponentRegistry::SerializeEntityComponents(entity));
			const glm::mat4 world = scene.GetWorldTransform(entity);
			result["world_position"] = FromVec3(glm::vec3(world[3]));
			return result;
		}

		Json RenderSettingsToJson(const RenderSettings& s)
		{
			return Json{
				{ "ShadowsEnabled", s.ShadowsEnabled }, { "ShadowMapSize", s.ShadowMapSize }, { "ShadowCascadeCount", s.ShadowCascadeCount },
				{ "ShadowDistance", s.ShadowDistance }, { "ShadowNormalBias", s.ShadowNormalBias },
				{ "SSAOEnabled", s.SSAOEnabled }, { "SSAORadius", s.SSAORadius }, { "SSAOBias", s.SSAOBias }, { "SSAOPower", s.SSAOPower },
				{ "SSAOStrength", s.SSAOStrength }, { "SSAOSampleCount", s.SSAOSampleCount },
				{ "Exposure", s.Exposure }, { "Tonemapper", static_cast<int>(s.Tonemapper) },
				{ "BackgroundColor", FromVec3(s.BackgroundColor) },
			};
		}

		void PatchRenderSettings(RenderSettings& s, const Json& v)
		{
			auto num = [&](const char* key, auto& field)
			{
				if (v.contains(key))
				{
					if (!v[key].is_number())
						throw CommandError(std::string(key) + " must be a number");
					field = v[key].get<std::decay_t<decltype(field)>>();
				}
			};
			auto flag = [&](const char* key, bool& field)
			{
				if (v.contains(key))
				{
					if (!v[key].is_boolean())
						throw CommandError(std::string(key) + " must be a boolean");
					field = v[key].get<bool>();
				}
			};
			flag("ShadowsEnabled", s.ShadowsEnabled);
			num("ShadowMapSize", s.ShadowMapSize);
			num("ShadowCascadeCount", s.ShadowCascadeCount);
			num("ShadowDistance", s.ShadowDistance);
			num("ShadowNormalBias", s.ShadowNormalBias);
			flag("SSAOEnabled", s.SSAOEnabled);
			num("SSAORadius", s.SSAORadius);
			num("SSAOBias", s.SSAOBias);
			num("SSAOPower", s.SSAOPower);
			num("SSAOStrength", s.SSAOStrength);
			num("SSAOSampleCount", s.SSAOSampleCount);
			num("Exposure", s.Exposure);
			if (v.contains("Tonemapper"))
			{
				const int op = v["Tonemapper"].get<int>();
				if (op < 0 || op > 2)
					throw CommandError("Tonemapper must be 0 (ACES), 1 (Reinhard) or 2 (None)");
				s.Tonemapper = static_cast<TonemapOperator>(op);
			}
			if (v.contains("BackgroundColor"))
				s.BackgroundColor = ToVec3(v["BackgroundColor"], "BackgroundColor");
		}

	}

	EditorCommands::EditorCommands(EditorContext& context)
		: m_Context(context)
	{
		RuntimeExecutable = FileSystem::GetExecutableDirectory() /
#if defined(EMBER_PLATFORM_WINDOWS)
			"EmberRuntime.exe";
#else
			"EmberRuntime";
#endif
		RegisterGeneral();
		RegisterProjectAndScene();
		RegisterEntities();
		RegisterAssets();
		RegisterPlayAndRender();
	}

	void EditorCommands::Register(const std::string& name, const std::string& category, const std::string& description, Json params,
		std::function<Json(const Json&)> handler)
	{
		m_Commands[name] = CommandInfo{ name, category, description, std::move(params), std::move(handler) };
	}

	Json EditorCommands::Execute(const std::string& method, const Json& params)
	{
		auto it = m_Commands.find(method);
		if (it == m_Commands.end())
			throw CommandError("unknown command '" + method + "' (use list_commands)");
		const Json& actualParams = params.is_null() ? Json::object() : params;
		if (!actualParams.is_object())
			throw CommandError("params must be a JSON object");
		return it->second.Handler(actualParams);
	}

	Json EditorCommands::HandleRequest(const Json& request)
	{
		Json response = Json::object();
		response["id"] = request.is_object() && request.contains("id") ? request["id"] : Json(nullptr);
		try
		{
			if (!request.is_object() || !request.contains("method") || !request["method"].is_string())
				throw CommandError("request must be an object with a string 'method'");
			const Json params = request.contains("params") ? request["params"] : Json::object();
			response["result"] = Execute(request["method"].get<std::string>(), params);
		}
		catch (const CommandError& e)
		{
			response["error"] = { { "message", e.what() } };
		}
		catch (const Json::exception& e)
		{
			response["error"] = { { "message", std::string("invalid parameter: ") + e.what() } };
		}
		catch (const std::exception& e)
		{
			response["error"] = { { "message", std::string("internal error: ") + e.what() } };
		}
		return response;
	}

	std::string EditorCommands::HandleLine(const std::string& line)
	{
		Json request;
		try
		{
			request = Json::parse(line);
		}
		catch (const Json::parse_error& e)
		{
			return Json{ { "id", nullptr }, { "error", { { "message", std::string("malformed JSON: ") + e.what() } } } }.dump();
		}
		return HandleRequest(request).dump();
	}

	// ----------------------------------------------------------------------------------------------

	namespace {

		// Resolves an entity parameter given as id (number or numeric string) or name.
		Entity ResolveEntity(Scene& scene, const Json& params, const char* key = "entity")
		{
			const Json& value = Require(params, key);
			if (value.is_string())
			{
				const std::string text = value.get<std::string>();
				uint64_t id = 0;
				auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), id);
				if (error != std::errc() || end != text.data() + text.size())
				{
					Entity byName = scene.FindEntityByName(text);
					if (!byName)
						throw CommandError("no entity named '" + text + "'");
					return byName;
				}
			}
			const UUID id = RequireEntityID(params, key);
			Entity entity = scene.GetEntityByUUID(id);
			if (!entity)
				throw CommandError("no entity with id " + std::to_string(static_cast<uint64_t>(id)));
			return entity;
		}

		const ComponentInfo& ResolveComponent(const Json& params)
		{
			const std::string name = RequireString(params, "component");
			const ComponentInfo* info = ComponentRegistry::Find(name);
			if (!info)
				throw CommandError("unknown component '" + name + "' (use list_component_types)");
			return *info;
		}

		void ApplyComponents(Entity entity, const Json& components)
		{
			if (components.is_null())
				return;
			if (!components.is_object())
				throw CommandError("'components' must be an object: {\"ComponentName\": {fields...}}");
			for (auto it = components.begin(); it != components.end(); ++it)
			{
				const ComponentInfo* info = ComponentRegistry::Find(it.key());
				if (!info)
					throw CommandError("unknown component '" + it.key() + "'");
				if (info->Name == "Relationship")
					throw CommandError("use the 'parent' parameter / set_parent instead of the Relationship component");
				if (!info->Deserialize(entity, ToYAML(it.value())))
					throw CommandError("invalid values for component '" + it.key() + "'");
			}
		}

		std::string ValidatedAssetPath(const Json& params, const char* key)
		{
			const std::string path = RequireString(params, key);
			if (!FileSystem::IsSafeRelativePath(path))
				throw CommandError(std::string("'") + key + "' must be a path relative to the asset directory, without '..'");
			return path;
		}

	}

	void EditorCommands::RegisterGeneral()
	{
		Register("ping", "General", "Checks the connection.", Json::object(), [](const Json&) { return Json("pong"); });

		Register("get_info", "General", "Engine, project, scene and play-state summary.", Json::object(), [this](const Json&)
		{
			Json info = {
				{ "engine", "Ember" }, { "version", "0.1.0" },
				{ "gpu", m_Context.GetGraphicsDevice().GetAdapterName() },
				{ "headless", m_Context.GetGraphicsDevice().IsHeadless() },
				{ "scene", m_Context.GetActiveScene()->GetName() },
				{ "scene_path", m_Context.GetScenePath() },
				{ "scene_dirty", m_Context.IsSceneDirty() },
				{ "entity_count", m_Context.GetActiveScene()->GetEntityCount() },
				{ "playing", m_Context.IsPlaying() }, { "paused", m_Context.IsPaused() },
			};
			if (const Ref<Project>& project = m_Context.GetProject())
			{
				info["project"] = project->GetConfig().Name;
				info["project_directory"] = project->GetProjectDirectory().string();
				info["asset_directory"] = project->GetAssetDirectory().string();
			}
			else
			{
				info["project"] = nullptr;
			}
			return info;
		});

		Register("list_commands", "General", "Lists all commands with descriptions and parameters.", { { "category", "string, optional - filter" } },
			[this](const Json& params)
		{
			const std::string category = GetString(params, "category");
			Json list = Json::array();
			for (const auto& [name, info] : m_Commands)
			{
				if (!category.empty() && info.Category != category)
					continue;
				list.push_back({ { "name", name }, { "category", info.Category }, { "description", info.Description }, { "params", info.Params } });
			}
			return list;
		});

		Register("get_logs", "General", "Returns log messages newer than 'since' (a sequence number from a previous call).",
			{ { "since", "integer, optional - last sequence number seen (default 0)" }, { "min_level", "string, optional - trace|info|warn|error" } },
			[](const Json& params)
		{
			const uint64_t since = static_cast<uint64_t>(GetNumber(params, "since", 0.0));
			const std::string minLevel = GetString(params, "min_level", "trace");
			const LogLevel threshold = minLevel == "error" ? LogLevel::Error : minLevel == "warn" ? LogLevel::Warn
				: minLevel == "info" ? LogLevel::Info : LogLevel::Trace;
			static const char* names[] = { "trace", "info", "warn", "error", "critical" };
			Json messages = Json::array();
			uint64_t last = since;
			for (const LogMessage& message : Log::GetHistory().GetSince(since))
			{
				last = message.Sequence;
				if (message.Level < threshold)
					continue;
				messages.push_back({ { "seq", message.Sequence }, { "level", names[static_cast<int>(message.Level)] },
					{ "logger", message.Logger }, { "text", message.Text } });
			}
			return Json{ { "messages", messages }, { "last", last } };
		});

		Register("quit", "General", "Closes the editor.", Json::object(), [this](const Json&)
		{
			if (OnQuit)
				OnQuit();
			return Json("ok");
		});

		Register("undo", "General", "Undoes the last edit (edit mode).", Json::object(), [this](const Json&)
		{
			return Json{ { "undone", m_Context.Undo() } };
		});
		Register("redo", "General", "Redoes the last undone edit (edit mode).", Json::object(), [this](const Json&)
		{
			return Json{ { "redone", m_Context.Redo() } };
		});
	}

	void EditorCommands::RegisterProjectAndScene()
	{
		Register("new_project", "Project", "Creates a project (directory with Assets/ and a start scene) and opens it.",
			{ { "path", "string - project directory" }, { "name", "string - project name" } }, [this](const Json& params)
		{
			std::string error;
			if (!m_Context.NewProject(RequireString(params, "path"), RequireString(params, "name"), &error))
				throw CommandError(error);
			return Json{ { "project_file", m_Context.GetProject()->GetProjectFile().string() }, { "scene_path", m_Context.GetScenePath() } };
		});

		Register("open_project", "Project", "Opens a project (.eproject file or its directory) and its start scene.",
			{ { "path", "string" } }, [this](const Json& params)
		{
			std::string error;
			if (!m_Context.OpenProject(RequireString(params, "path"), &error))
				throw CommandError(error);
			return Json{ { "project", m_Context.GetProject()->GetConfig().Name }, { "scene_path", m_Context.GetScenePath() } };
		});

		Register("get_project", "Project", "Returns the project settings.", Json::object(), [this](const Json&)
		{
			const ProjectConfig& config = m_Context.RequireProject().GetConfig();
			return Json{ { "Name", config.Name }, { "AssetDirectory", config.AssetDirectory.generic_string() },
				{ "StartScene", config.StartScene }, { "WindowWidth", config.WindowWidth }, { "WindowHeight", config.WindowHeight },
				{ "Fullscreen", config.Fullscreen }, { "VSync", config.VSync }, { "RenderQuality", config.RenderQuality } };
		});

		Register("set_project", "Project", "Changes project settings (any subset) and saves the project file.",
			{ { "Name", "string" }, { "StartScene", "string - asset path" }, { "WindowWidth", "integer" }, { "WindowHeight", "integer" },
			  { "Fullscreen", "boolean" }, { "VSync", "boolean" }, { "RenderQuality", "string - Auto|Low|High" } }, [this](const Json& params)
		{
			Project& project = m_Context.RequireProject();
			ProjectConfig& config = project.GetConfig();
			config.Name = GetString(params, "Name", config.Name);
			if (params.contains("StartScene"))
			{
				const std::string scene = ValidatedAssetPath(params, "StartScene");
				config.StartScene = scene;
			}
			config.WindowWidth = static_cast<uint32_t>(GetNumber(params, "WindowWidth", config.WindowWidth));
			config.WindowHeight = static_cast<uint32_t>(GetNumber(params, "WindowHeight", config.WindowHeight));
			config.Fullscreen = GetBool(params, "Fullscreen", config.Fullscreen);
			config.VSync = GetBool(params, "VSync", config.VSync);
			config.RenderQuality = GetString(params, "RenderQuality", config.RenderQuality);
			if (!project.Save())
				throw CommandError("failed to save the project file");
			return Json("ok");
		});

		Register("new_scene", "Scene", "Replaces the edited scene with an empty one (unsaved).", { { "name", "string, optional" } },
			[this](const Json& params)
		{
			m_Context.NewScene(GetString(params, "name", "Untitled"));
			return Json("ok");
		});

		Register("open_scene", "Scene", "Opens a scene file.", { { "path", "string - asset path, e.g. Scenes/Level1.escene" } },
			[this](const Json& params)
		{
			std::string error;
			if (!m_Context.OpenScene(ValidatedAssetPath(params, "path"), &error))
				throw CommandError(error);
			return Json{ { "entity_count", m_Context.GetEditorScene()->GetEntityCount() } };
		});

		Register("save_scene", "Scene", "Saves the edited scene (to 'path', or its current file).", { { "path", "string, optional - asset path" } },
			[this](const Json& params)
		{
			std::string error;
			const std::string path = params.contains("path") ? ValidatedAssetPath(params, "path") : std::string();
			if (!m_Context.SaveScene(path, &error))
				throw CommandError(error);
			return Json{ { "path", m_Context.GetScenePath() } };
		});

		Register("get_scene", "Scene", "Returns the active scene as YAML text (the .escene format).", Json::object(), [this](const Json&)
		{
			return Json{ { "yaml", SceneSerializer(m_Context.GetActiveScene()).SerializeToString() } };
		});

		Register("load_scene_data", "Scene", "Replaces the edited scene with the given .escene YAML text (undoable).",
			{ { "yaml", "string - scene in .escene format" } }, [this](const Json& params)
		{
			if (m_Context.IsPlaying())
				throw CommandError("stop play mode first");
			Ref<Scene> probe = CreateRef<Scene>();
			std::string error;
			const std::string yaml = RequireString(params, "yaml");
			if (!SceneSerializer(probe).DeserializeFromString(yaml, &error))
				throw CommandError(error);
			m_Context.RecordUndo("Load scene data");
			if (!SceneSerializer(m_Context.GetEditorScene()).DeserializeFromString(yaml, &error))
				throw CommandError(error);
			return Json{ { "entity_count", m_Context.GetEditorScene()->GetEntityCount() } };
		});
	}

	void EditorCommands::RegisterEntities()
	{
		Register("list_entities", "Entity", "Lists all entities of the active scene (id, name, parent, children, component names).",
			Json::object(), [this](const Json&)
		{
			Scene& scene = *m_Context.GetActiveScene();
			Json list = Json::array();
			for (Entity entity : scene.GetAllEntitiesOrdered())
				list.push_back(EntitySummary(scene, entity));
			return list;
		});

		Register("get_entity", "Entity", "Returns an entity with all component data.", { { "entity", "id or name" } }, [this](const Json& params)
		{
			Scene& scene = *m_Context.GetActiveScene();
			return EntityDetails(scene, ResolveEntity(scene, params));
		});

		Register("find_entities", "Entity", "Finds entities by name substring and/or component.",
			{ { "name", "string, optional - case-sensitive substring" }, { "component", "string, optional" } }, [this](const Json& params)
		{
			Scene& scene = *m_Context.GetActiveScene();
			const std::string name = GetString(params, "name");
			const ComponentInfo* component = params.contains("component") ? &ResolveComponent(params) : nullptr;
			Json list = Json::array();
			for (Entity entity : scene.GetAllEntitiesOrdered())
			{
				if (!name.empty() && entity.GetName().find(name) == std::string::npos)
					continue;
				if (component && !component->Has(entity))
					continue;
				list.push_back(EntitySummary(scene, entity));
			}
			return list;
		});

		Register("create_entity", "Entity", "Creates an entity, optionally with a parent and components.",
			{ { "name", "string, optional" }, { "parent", "id or name, optional" },
			  { "components", "object, optional - {\"MeshRenderer\": {\"Mesh\": \"builtin://Cube\"}, \"Transform\": {\"Translation\": [0,1,0]}}" } },
			[this](const Json& params)
		{
			Scene& scene = *m_Context.GetActiveScene();
			Entity parent = params.contains("parent") && !params["parent"].is_null() ? ResolveEntity(scene, params, "parent") : Entity();
			m_Context.RecordUndo("Create entity");
			Entity entity = scene.CreateEntity(GetString(params, "name", "Entity"));
			if (parent)
				scene.SetParent(entity, parent, false);
			try
			{
				ApplyComponents(entity, params.contains("components") ? params["components"] : Json());
			}
			catch (const CommandError&)
			{
				scene.DestroyEntity(entity);
				scene.FlushPendingDestruction();
				throw;
			}
			return EntitySummary(scene, entity);
		});

		Register("destroy_entity", "Entity", "Destroys an entity and its children.", { { "entity", "id or name" } }, [this](const Json& params)
		{
			Scene& scene = *m_Context.GetActiveScene();
			Entity entity = ResolveEntity(scene, params);
			m_Context.RecordUndo("Destroy entity");
			if (m_Context.GetSelectedEntity() == entity)
				m_Context.ClearSelection();
			scene.DestroyEntity(entity);
			return Json("ok");
		});

		Register("duplicate_entity", "Entity", "Duplicates an entity (with children); returns the copy.", { { "entity", "id or name" } },
			[this](const Json& params)
		{
			Scene& scene = *m_Context.GetActiveScene();
			Entity entity = ResolveEntity(scene, params);
			m_Context.RecordUndo("Duplicate entity");
			return EntitySummary(scene, scene.DuplicateEntity(entity));
		});

		Register("rename_entity", "Entity", "Renames an entity.", { { "entity", "id or name" }, { "name", "string" } }, [this](const Json& params)
		{
			Scene& scene = *m_Context.GetActiveScene();
			Entity entity = ResolveEntity(scene, params);
			const std::string name = RequireString(params, "name");
			m_Context.RecordUndo("Rename entity");
			entity.GetComponent<TagComponent>().Tag = name;
			return Json("ok");
		});

		Register("set_parent", "Entity", "Re-parents an entity (parent null = root).",
			{ { "entity", "id or name" }, { "parent", "id or name, or null" }, { "keep_world_transform", "boolean, optional (default true)" } },
			[this](const Json& params)
		{
			Scene& scene = *m_Context.GetActiveScene();
			Entity entity = ResolveEntity(scene, params);
			Entity parent = params.contains("parent") && !params["parent"].is_null() ? ResolveEntity(scene, params, "parent") : Entity();
			m_Context.RecordUndo("Set parent");
			if (!scene.SetParent(entity, parent, GetBool(params, "keep_world_transform", true)))
				throw CommandError("cannot parent an entity to itself or its descendants");
			return Json("ok");
		});

		Register("select_entity", "Entity", "Selects an entity in the editor (null clears the selection).", { { "entity", "id or name, or null" } },
			[this](const Json& params)
		{
			if (!params.contains("entity") || params["entity"].is_null())
			{
				m_Context.ClearSelection();
				return Json("ok");
			}
			Scene& scene = *m_Context.GetActiveScene();
			m_Context.SetSelectedEntity(ResolveEntity(scene, params).GetUUID());
			return Json("ok");
		});

		Register("list_component_types", "Component", "Lists component types with their fields and default values.", Json::object(),
			[](const Json&)
		{
			Scene scratch;
			Json list = Json::array();
			for (const ComponentInfo& info : ComponentRegistry::GetAll())
			{
				if (info.Name == "Relationship")
					continue;
				Entity entity = scratch.CreateEntity("Defaults");
				info.Add(entity);
				list.push_back({ { "name", info.Name }, { "category", info.Category }, { "user_addable", info.UserAddable },
					{ "defaults", FromYAML(info.Serialize(entity)) } });
			}
			return list;
		});

		Register("get_component", "Component", "Returns one component's data.", { { "entity", "id or name" }, { "component", "string" } },
			[this](const Json& params)
		{
			Scene& scene = *m_Context.GetActiveScene();
			Entity entity = ResolveEntity(scene, params);
			const ComponentInfo& info = ResolveComponent(params);
			if (!info.Has(entity))
				throw CommandError("entity '" + entity.GetName() + "' has no " + info.Name + " component");
			return FromYAML(info.Serialize(entity));
		});

		auto setComponent = [this](const Json& params)
		{
			Scene& scene = *m_Context.GetActiveScene();
			Entity entity = ResolveEntity(scene, params);
			const ComponentInfo& info = ResolveComponent(params);
			if (info.Name == "Relationship")
				throw CommandError("use set_parent to change the hierarchy");
			const Json values = params.contains("values") ? params["values"] : Json::object();
			if (!values.is_object())
				throw CommandError("'values' must be an object");
			m_Context.RecordUndo("Set " + info.Name);
			if (!info.Deserialize(entity, ToYAML(values)))
				throw CommandError("some values for " + info.Name + " were invalid (valid fields were applied)");
			return FromYAML(info.Serialize(entity));
		};
		const Json setParams = { { "entity", "id or name" }, { "component", "string" }, { "values", "object, optional - fields to set (others unchanged)" } };
		Register("add_component", "Component", "Adds a component (or updates it if present) and returns its data.", setParams, setComponent);
		Register("set_component", "Component", "Sets component fields (adds the component if missing) and returns its data.", setParams, setComponent);

		Register("remove_component", "Component", "Removes a component.", { { "entity", "id or name" }, { "component", "string" } },
			[this](const Json& params)
		{
			Scene& scene = *m_Context.GetActiveScene();
			Entity entity = ResolveEntity(scene, params);
			const ComponentInfo& info = ResolveComponent(params);
			if (!info.UserAddable)
				throw CommandError(info.Name + " cannot be removed");
			if (!info.Has(entity))
				throw CommandError("entity '" + entity.GetName() + "' has no " + info.Name + " component");
			m_Context.RecordUndo("Remove " + info.Name);
			info.Remove(entity);
			return Json("ok");
		});

		Register("set_transform", "Component", "Sets local transform values (any subset).",
			{ { "entity", "id or name" }, { "position", "[x,y,z], optional" }, { "rotation_degrees", "[x,y,z], optional" },
			  { "rotation", "[x,y,z] radians, optional" }, { "scale", "[x,y,z] or number, optional" } }, [this](const Json& params)
		{
			Scene& scene = *m_Context.GetActiveScene();
			Entity entity = ResolveEntity(scene, params);
			m_Context.RecordUndo("Set transform");
			auto& transform = entity.GetComponent<TransformComponent>();
			if (params.contains("position"))
				transform.Translation = ToVec3(params["position"], "position");
			if (params.contains("rotation_degrees"))
				transform.Rotation = glm::radians(ToVec3(params["rotation_degrees"], "rotation_degrees"));
			if (params.contains("rotation"))
				transform.Rotation = ToVec3(params["rotation"], "rotation");
			if (params.contains("scale"))
				transform.Scale = params["scale"].is_number() ? glm::vec3(params["scale"].get<float>()) : ToVec3(params["scale"], "scale");
			return FromYAML(ComponentRegistry::Find("Transform")->Serialize(entity));
		});

		Register("create_prefab", "Prefab", "Saves an entity (and children) as a prefab asset.",
			{ { "entity", "id or name" }, { "path", "string - asset path, e.g. Prefabs/Enemy.eprefab" } }, [this](const Json& params)
		{
			Project& project = m_Context.RequireProject();
			Scene& scene = *m_Context.GetActiveScene();
			Entity entity = ResolveEntity(scene, params);
			const std::string path = ValidatedAssetPath(params, "path");
			if (!SceneSerializer::SerializePrefab(entity, project.GetAssetPath(path)))
				throw CommandError("failed to write the prefab");
			return Json{ { "path", path } };
		});

		Register("instantiate_prefab", "Prefab", "Instantiates a prefab into the active scene.",
			{ { "path", "string - asset path" }, { "position", "[x,y,z], optional" }, { "parent", "id or name, optional" } },
			[this](const Json& params)
		{
			Project& project = m_Context.RequireProject();
			Scene& scene = *m_Context.GetActiveScene();
			const std::string path = ValidatedAssetPath(params, "path");
			Entity parent = params.contains("parent") && !params["parent"].is_null() ? ResolveEntity(scene, params, "parent") : Entity();
			m_Context.RecordUndo("Instantiate prefab");
			std::string error;
			Entity root = SceneSerializer::InstantiatePrefab(scene, project.GetAssetPath(path), path, &error);
			if (!root)
				throw CommandError(error);
			if (parent)
				scene.SetParent(root, parent, false);
			if (params.contains("position"))
				root.GetComponent<TransformComponent>().Translation = ToVec3(params["position"], "position");
			return EntitySummary(scene, root);
		});
	}

	void EditorCommands::RegisterAssets()
	{
		Register("list_assets", "Asset", "Lists files in the asset directory (or a sub-directory).",
			{ { "directory", "string, optional - asset sub-directory" }, { "recursive", "boolean, optional (default true)" } },
			[this](const Json& params)
		{
			Project& project = m_Context.RequireProject();
			std::filesystem::path root = project.GetAssetDirectory();
			if (params.contains("directory"))
				root /= ValidatedAssetPath(params, "directory");
			std::error_code ec;
			Json files = Json::array();
			auto add = [&](const std::filesystem::directory_entry& entry)
			{
				files.push_back({ { "path", entry.path().lexically_relative(project.GetAssetDirectory()).generic_string() },
					{ "directory", entry.is_directory() }, { "size", entry.is_regular_file() ? entry.file_size() : 0 } });
			};
			if (GetBool(params, "recursive", true))
			{
				for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec))
					add(entry);
			}
			else
			{
				for (const auto& entry : std::filesystem::directory_iterator(root, ec))
					add(entry);
			}
			if (ec)
				throw CommandError("cannot list '" + root.string() + "': " + ec.message());
			std::sort(files.begin(), files.end(), [](const Json& a, const Json& b) { return a["path"] < b["path"]; });
			return files;
		});

		Register("read_asset", "Asset", "Returns the text content of an asset (scripts, scenes, prefabs, materials).", { { "path", "string" } },
			[this](const Json& params)
		{
			Project& project = m_Context.RequireProject();
			std::optional<std::string> text = FileSystem::ReadText(project.GetAssetPath(ValidatedAssetPath(params, "path")));
			if (!text)
				throw CommandError("cannot read the asset");
			return Json{ { "content", *text } };
		});

		Register("write_asset", "Asset", "Creates or overwrites a text asset (e.g. a Lua script). Directories are created as needed.",
			{ { "path", "string - asset path" }, { "content", "string" } }, [this](const Json& params)
		{
			Project& project = m_Context.RequireProject();
			const std::string path = ValidatedAssetPath(params, "path");
			if (!FileSystem::WriteText(project.GetAssetPath(path), RequireString(params, "content")))
				throw CommandError("failed to write the asset");
			m_Context.GetRenderAssets().ClearCaches();
			return Json{ { "path", path } };
		});

		Register("delete_asset", "Asset", "Deletes an asset file or directory.", { { "path", "string" } }, [this](const Json& params)
		{
			Project& project = m_Context.RequireProject();
			std::error_code ec;
			const std::uintmax_t removed = std::filesystem::remove_all(project.GetAssetPath(ValidatedAssetPath(params, "path")), ec);
			if (ec || removed == 0)
				throw CommandError("nothing was deleted");
			m_Context.GetRenderAssets().ClearCaches();
			return Json{ { "removed", removed } };
		});

		Register("import_file", "Asset", "Copies an external file (model, texture, HDRI, audio, font) into the asset directory.",
			{ { "source", "string - absolute path of the file to import" }, { "destination", "string - asset path" } }, [this](const Json& params)
		{
			Project& project = m_Context.RequireProject();
			const std::filesystem::path source = RequireString(params, "source");
			const std::string destination = ValidatedAssetPath(params, "destination");
			std::error_code ec;
			if (!std::filesystem::is_regular_file(source, ec))
				throw CommandError("source file not found: " + source.string());
			const std::filesystem::path target = project.GetAssetPath(destination);
			std::filesystem::create_directories(target.parent_path(), ec);
			std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, ec);
			if (ec)
				throw CommandError("copy failed: " + ec.message());
			m_Context.GetRenderAssets().ClearCaches();
			return Json{ { "path", destination } };
		});

		Register("create_material", "Asset", "Writes a material asset (.ematerial) usable by MeshRenderer.Material.",
			{ { "path", "string - asset path, e.g. Materials/Gold.ematerial" },
			  { "values", "object - BaseColor, Metallic, Roughness, EmissiveColor, EmissiveStrength, AlphaMode, *Texture, ..." } },
			[this](const Json& params)
		{
			Project& project = m_Context.RequireProject();
			const std::string path = ValidatedAssetPath(params, "path");
			MaterialParameters material;
			if (params.contains("values") && !MaterialSerializer::FromYAML(ToYAML(params["values"]), material))
				throw CommandError("invalid material values");
			if (!MaterialSerializer::Save(material, project.GetAssetPath(path)))
				throw CommandError("failed to write the material");
			m_Context.GetRenderAssets().ClearCaches();
			return Json{ { "path", path }, { "material", FromYAML(MaterialSerializer::ToYAML(material)) } };
		});
	}

	void EditorCommands::RegisterPlayAndRender()
	{
		Register("play", "Play", "Enters play mode (simulates a copy of the edited scene).", Json::object(), [this](const Json&)
		{
			m_Context.Play();
			return Json("ok");
		});
		Register("stop", "Play", "Leaves play mode (runtime changes are discarded).", Json::object(), [this](const Json&)
		{
			m_Context.Stop();
			return Json("ok");
		});
		Register("pause", "Play", "Pauses or resumes play mode.", { { "paused", "boolean, optional (default true)" } }, [this](const Json& params)
		{
			if (!m_Context.IsPlaying())
				throw CommandError("not in play mode");
			m_Context.SetPaused(GetBool(params, "paused", true));
			return Json("ok");
		});
		Register("get_play_state", "Play", "Returns play state, simulated time and script diagnostics.", Json::object(), [this](const Json&)
		{
			Json state = { { "playing", m_Context.IsPlaying() }, { "paused", m_Context.IsPaused() } };
			if (m_Context.IsPlaying())
			{
				Scene& scene = *m_Context.GetActiveScene();
				state["time"] = scene.GetRuntimeTime();
				state["entity_count"] = scene.GetEntityCount();
				if (ScriptEngine* scripts = scene.GetScriptEngine())
				{
					state["script_errors"] = scripts->GetErrors();
					Json tests = Json::array();
					for (const ScriptTestResult& result : scripts->GetTestResults())
						tests.push_back({ { "name", result.Name }, { "passed", result.Passed }, { "message", result.Message } });
					state["test_results"] = tests;
				}
			}
			return state;
		});

		Register("run_frames", "Play", "Simulates N frames immediately (deterministic; starts play mode if needed).",
			{ { "frames", "integer" }, { "dt", "number, optional - seconds per frame (default 1/60)" } }, [this](const Json& params)
		{
			const int frames = static_cast<int>(GetNumber(params, "frames", 1.0));
			const float dt = static_cast<float>(GetNumber(params, "dt", 1.0 / 60.0));
			if (frames < 0 || frames > 100000 || dt <= 0.0f || dt > 1.0f)
				throw CommandError("frames must be 0..100000 and dt in (0, 1]");
			if (!m_Context.IsPlaying())
				m_Context.Play();
			Scene& scene = *m_Context.GetActiveScene();
			const bool paused = scene.IsPaused();
			scene.SetPaused(false);
			for (int i = 0; i < frames; i++)
			{
				scene.OnUpdateRuntime(Timestep(dt));
				Input::EndFrame();
			}
			scene.SetPaused(paused);
			Json result = { { "time", scene.GetRuntimeTime() }, { "entity_count", scene.GetEntityCount() } };
			if (ScriptEngine* scripts = scene.GetScriptEngine())
				result["script_error_count"] = scripts->GetErrorCount();
			return result;
		});

		Register("step", "Play", "Pauses and advances play mode by N frames on subsequent editor frames.", { { "frames", "integer, optional (default 1)" } },
			[this](const Json& params)
		{
			if (!m_Context.IsPlaying())
				throw CommandError("not in play mode");
			m_Context.Step(static_cast<int>(GetNumber(params, "frames", 1.0)));
			return Json("ok");
		});

		Register("send_input", "Play", "Injects keyboard/mouse input into the game (as if from the window).",
			{ { "key", "string, optional - e.g. W, Space, Left, Escape" }, { "mouse_button", "string, optional - Left|Right|Middle" },
			  { "action", "string - press|release" }, { "mouse_position", "[x,y], optional - move the mouse" }, { "scroll", "number, optional" } },
			[](const Json& params)
		{
			const std::string action = GetString(params, "action", "press");
			if (action != "press" && action != "release")
				throw CommandError("action must be 'press' or 'release' (hold a key across run_frames calls)");
			const bool press = action == "press";
			if (params.contains("key"))
			{
				const KeyCode key = KeyCodeFromString(RequireString(params, "key"));
				if (key == KeyCode::Unknown)
					throw CommandError("unknown key name");
				if (press)
				{
					KeyPressedEvent event(key, false);
					Input::OnEvent(event);
				}
				else
				{
					KeyReleasedEvent event(key);
					Input::OnEvent(event);
				}
			}
			if (params.contains("mouse_button"))
			{
				bool valid = false;
				const MouseButton button = MouseButtonFromString(RequireString(params, "mouse_button"), &valid);
				if (!valid)
					throw CommandError("unknown mouse button");
				if (press)
				{
					MouseButtonPressedEvent event(button);
					Input::OnEvent(event);
				}
				else
				{
					MouseButtonReleasedEvent event(button);
					Input::OnEvent(event);
				}
			}
			if (params.contains("mouse_position"))
			{
				const Json& position = params["mouse_position"];
				if (!position.is_array() || position.size() != 2)
					throw CommandError("mouse_position must be [x, y]");
				MouseMovedEvent event(position[0].get<float>(), position[1].get<float>());
				Input::OnEvent(event);
			}
			if (params.contains("scroll"))
			{
				MouseScrolledEvent event(0.0f, static_cast<float>(GetNumber(params, "scroll", 0.0)));
				Input::OnEvent(event);
			}
			return Json("ok");
		});

		Register("execute_lua", "Play", "Runs Lua code in the running game's script environment and returns the result.",
			{ { "code", "string" } }, [this](const Json& params)
		{
			if (!m_Context.IsPlaying())
				throw CommandError("execute_lua requires play mode");
			ScriptEngine* scripts = m_Context.GetActiveScene()->GetScriptEngine();
			std::string result, error;
			if (!scripts->ExecuteString(RequireString(params, "code"), &result, &error))
				throw CommandError("Lua error: " + error);
			return Json{ { "result", result } };
		});

		Register("capture", "Render", "Renders the active scene to a PNG file; returns its path and render statistics.",
			{ { "path", "string, optional - output file (default: <project>/Screenshots/capture_N.png)" },
			  { "width", "integer, optional (default 960)" }, { "height", "integer, optional (default 540)" },
			  { "camera", "string, optional - 'editor' or 'game' (default: game when playing or when the scene has a camera)" } },
			[this](const Json& params)
		{
			static int s_Counter = 0;
			std::filesystem::path path = GetString(params, "path");
			if (path.empty())
			{
				const std::filesystem::path base = m_Context.HasProject() ? m_Context.GetProject()->GetProjectDirectory() / "Screenshots"
					: std::filesystem::temp_directory_path() / "ember_captures";
				path = base / ("capture_" + std::to_string(++s_Counter) + ".png");
			}
			const uint32_t width = static_cast<uint32_t>(GetNumber(params, "width", 960.0));
			const uint32_t height = static_cast<uint32_t>(GetNumber(params, "height", 540.0));
			std::string camera = GetString(params, "camera");
			if (camera.empty())
				camera = (m_Context.IsPlaying() || m_Context.GetActiveScene()->GetPrimaryCameraEntity()) ? "game" : "editor";
			if (camera != "game" && camera != "editor")
				throw CommandError("camera must be 'game' or 'editor'");
			std::string error;
			if (!m_Context.CaptureToFile(path, width, height, camera == "game", &error))
				throw CommandError(error);
			std::error_code ec;
			const RenderStats stats = m_Context.GetCaptureStats();
			const Json statsJson = { { "draw_calls", stats.DrawCalls }, { "shadow_draw_calls", stats.ShadowDrawCalls },
				{ "triangles", stats.Triangles }, { "culled_objects", stats.CulledObjects }, { "ui_elements", stats.UIElements } };
			return Json{ { "path", std::filesystem::absolute(path, ec).string() }, { "width", width }, { "height", height }, { "camera", camera },
				{ "stats", statsJson } };
		});

		Register("set_editor_camera", "Render", "Positions the editor camera.",
			{ { "position", "[x,y,z]" }, { "target", "[x,y,z]" } }, [this](const Json& params)
		{
			m_Context.GetEditorCamera().LookAt(ToVec3(Require(params, "position"), "position"), ToVec3(Require(params, "target"), "target"));
			return Json("ok");
		});

		Register("focus_entity", "Render", "Points the editor camera at an entity.", { { "entity", "id or name" } }, [this](const Json& params)
		{
			Scene& scene = *m_Context.GetActiveScene();
			Entity entity = ResolveEntity(scene, params);
			const glm::mat4 world = scene.GetWorldTransform(entity);
			float radius = 1.0f;
			if (const auto* renderer = entity.TryGetComponent<MeshRendererComponent>())
			{
				if (Ref<Model> model = m_Context.GetRenderAssets().GetModel(renderer->Mesh))
				{
					const AABB bounds = model->MeshData->Bounds.Transformed(world);
					radius = glm::length(bounds.Extents());
					m_Context.GetEditorCamera().Focus(bounds.Center(), radius);
					return Json("ok");
				}
			}
			m_Context.GetEditorCamera().Focus(glm::vec3(world[3]), radius);
			return Json("ok");
		});

		Register("pick_entity", "Render", "Returns the entity at a pixel of the last capture or viewport render.",
			{ { "x", "integer" }, { "y", "integer" }, { "source", "string, optional - 'capture' (default) or 'viewport'" } }, [this](const Json& params)
		{
			const uint32_t x = static_cast<uint32_t>(GetNumber(params, "x", 0.0));
			const uint32_t y = static_cast<uint32_t>(GetNumber(params, "y", 0.0));
			const UUID id = m_Context.PickEntity(x, y, GetString(params, "source", "capture") == "viewport");
			Scene& scene = *m_Context.GetActiveScene();
			Entity entity = scene.GetEntityByUUID(id);
			return entity ? EntitySummary(scene, entity) : Json(nullptr);
		});

		Register("get_render_settings", "Render", "Returns renderer quality settings.", Json::object(), [this](const Json&)
		{
			return RenderSettingsToJson(m_Context.GetViewportRenderer().GetSettings());
		});

		Register("set_render_settings", "Render", "Changes renderer settings (any subset of get_render_settings fields).",
			{ { "...", "fields from get_render_settings" } }, [this](const Json& params)
		{
			RenderSettings settings = m_Context.GetViewportRenderer().GetSettings();
			PatchRenderSettings(settings, params);
			m_Context.GetViewportRenderer().GetSettings() = settings;
			return RenderSettingsToJson(settings);
		});

		Register("export_game", "Build", "Exports the project as a standalone game (runtime + assets) into a directory.",
			{ { "output", "string - destination directory (outside the project)" } }, [this](const Json& params)
		{
			Project& project = m_Context.RequireProject();
			if (m_Context.IsSceneDirty() && !m_Context.GetScenePath().empty())
				EMBER_CORE_WARN("Exporting with unsaved scene changes (the saved file is exported)");
			const ExportResult result = ExportGame(project, RequireString(params, "output"), RuntimeExecutable);
			if (!result.Success)
				throw CommandError(result.Error);
			return Json{ { "executable", result.Executable.string() }, { "files", result.FilesCopied } };
		});
	}

}
