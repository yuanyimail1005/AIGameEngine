// ImGui user interface of the editor. All scene changes go through EditorContext / EditorCommands so the UI, the
// AI control interface and undo/redo stay consistent.

#include "EditorLayer.h"

#include "Ember/Core/Application.h"
#include "Ember/Core/FileSystem.h"
#include "Ember/Core/Input.h"
#include "Ember/Core/YAMLUtils.h"
#include "Ember/Core/Log.h"
#include "Ember/ImGui/ImGuiLayer.h"
#include "Ember/Math/Math.h"
#include "Ember/Renderer/GraphicsDevice.h"
#include "Ember/Scene/ComponentRegistry.h"
#include "Ember/Scene/SceneSerializer.h"

#include <ImGuizmo.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstring>

namespace Ember {

	namespace {

		constexpr const char* c_AssetPayload = "EMBER_ASSET";
		constexpr const char* c_EntityPayload = "EMBER_ENTITY";

		struct EnumOptions
		{
			const char* Key;
			std::vector<const char*> Values;
		};

		const std::vector<EnumOptions>& GetEnumOptions()
		{
			static const std::vector<EnumOptions> s_Options = {
				{ "Type", { "Static", "Dynamic", "Kinematic" } },
				{ "ProjectionType", { "Perspective", "Orthographic" } },
				{ "AlphaMode", { "Opaque", "Mask", "Blend" } },
				{ "Alignment", { "Left", "Center", "Right" } },
			};
			return s_Options;
		}

		bool IsAssetField(const std::string& key)
		{
			return key == "Mesh" || key == "Material" || key == "Script" || key == "Clip" || key == "EnvironmentMap" ||
				key == "Prefab" || key == "Font" || (key.size() >= 7 && key.substr(key.size() - 7) == "Texture");
		}

		bool IsNumber(const std::string& text, double& value)
		{
			if (text.empty())
				return false;
			char* end = nullptr;
			value = std::strtod(text.c_str(), &end);
			return end == text.c_str() + text.size();
		}

		std::string Lower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		// Draws an editor widget for one YAML value. On change, writes the new value into patch[key].
		// Returns true when the user changed something. activated is set when an edit begins (for undo).
		bool DrawField(const std::string& key, const YAML::Node& value, YAML::Node& patch, bool& activated, int depth = 0)
		{
			ImGui::PushID(key.c_str());
			bool changed = false;

			if (value.IsMap())
			{
				if (ImGui::TreeNodeEx(key.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth))
				{
					YAML::Node childPatch(YAML::NodeType::Map);
					for (const auto& item : value)
						changed |= DrawField(item.first.as<std::string>(), item.second, childPatch, activated, depth + 1);
					if (changed)
						patch[key] = childPatch;
					ImGui::TreePop();
				}
				ImGui::PopID();
				return changed;
			}

			if (value.IsSequence())
			{
				std::vector<float> numbers;
				for (const YAML::Node& element : value)
				{
					double number = 0.0;
					if (!element.IsScalar() || !IsNumber(element.Scalar(), number))
					{
						numbers.clear();
						break;
					}
					numbers.push_back(static_cast<float>(number));
				}
				if (numbers.size() >= 2 && numbers.size() <= 4)
				{
					const bool isColor = key.find("Color") != std::string::npos;
					if (isColor && numbers.size() == 3)
						changed = ImGui::ColorEdit3(key.c_str(), numbers.data(), ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
					else if (isColor && numbers.size() == 4)
						changed = ImGui::ColorEdit4(key.c_str(), numbers.data(), ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
					else if (numbers.size() == 2)
						changed = ImGui::DragFloat2(key.c_str(), numbers.data(), 0.05f);
					else if (numbers.size() == 3)
						changed = ImGui::DragFloat3(key.c_str(), numbers.data(), 0.05f);
					else
						changed = ImGui::DragFloat4(key.c_str(), numbers.data(), 0.05f);
					activated |= ImGui::IsItemActivated();
					if (changed)
					{
						YAML::Node sequence(YAML::NodeType::Sequence);
						for (float number : numbers)
							sequence.push_back(number);
						patch[key] = sequence;
					}
				}
				else
				{
					ImGui::TextDisabled("%s: [%zu items]", key.c_str(), value.size());
				}
				ImGui::PopID();
				return changed;
			}

			const std::string text = value.IsScalar() ? value.Scalar() : std::string();
			for (const EnumOptions& options : GetEnumOptions())
			{
				if (key != options.Key)
					continue;
				int current = 0;
				for (size_t i = 0; i < options.Values.size(); i++)
				{
					if (text == options.Values[i])
						current = static_cast<int>(i);
				}
				if (ImGui::Combo(key.c_str(), &current, options.Values.data(), static_cast<int>(options.Values.size())))
				{
					activated = true;
					patch[key] = options.Values[static_cast<size_t>(current)];
					changed = true;
				}
				ImGui::PopID();
				return changed;
			}

			// Free text (may look like a number, e.g. a score) - multi-line editing.
			if (key == "Text")
			{
				std::string buffer = text;
				ImGui::InputTextMultiline(key.c_str(), &buffer, ImVec2(0.0f, ImGui::GetTextLineHeight() * 3.5f));
				activated |= ImGui::IsItemActivated();
				if (ImGui::IsItemDeactivatedAfterEdit() && buffer != text)
				{
					patch[key] = buffer;
					changed = true;
				}
				ImGui::PopID();
				return changed;
			}

			double number = 0.0;
			if (text == "true" || text == "false")
			{
				bool flag = text == "true";
				if (ImGui::Checkbox(key.c_str(), &flag))
				{
					activated = true;
					patch[key] = flag;
					changed = true;
				}
			}
			else if (key == "SortOrder" && IsNumber(text, number))
			{
				int intValue = static_cast<int>(number);
				changed = ImGui::DragInt(key.c_str(), &intValue, 0.1f);
				activated |= ImGui::IsItemActivated();
				if (changed)
					patch[key] = intValue;
			}
			else if (IsNumber(text, number) && !IsAssetField(key) && key != "Tag")
			{
				float floatValue = static_cast<float>(number);
				const bool isAngle = key.find("Angle") != std::string::npos || key == "PerspectiveFOV" || key == "Rotation";
				if (isAngle)
				{
					float degrees = glm::degrees(floatValue);
					changed = ImGui::DragFloat((key + " (deg)").c_str(), &degrees, 0.5f);
					floatValue = glm::radians(degrees);
				}
				else
				{
					changed = ImGui::DragFloat(key.c_str(), &floatValue, 0.01f);
				}
				activated |= ImGui::IsItemActivated();
				if (changed)
					patch[key] = floatValue;
			}
			else
			{
				std::string buffer = text;
				ImGui::InputText(key.c_str(), &buffer);
				activated |= ImGui::IsItemActivated();
				const bool committed = ImGui::IsItemDeactivatedAfterEdit();
				if (IsAssetField(key) && ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(c_AssetPayload))
					{
						buffer.assign(static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize));
						activated = true;
						patch[key] = buffer;
						changed = true;
					}
					ImGui::EndDragDropTarget();
				}
				if (committed && buffer != text)
				{
					patch[key] = buffer;
					changed = true;
				}
			}
			ImGui::PopID();
			return changed;
		}

	}

	void EditorLayer::DrawDockspace()
	{
		ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
	}

	void EditorLayer::DrawMenuBar()
	{
		if (!ImGui::BeginMainMenuBar())
			return;

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Project..."))
			{
				m_PathPopup = "new_project";
				m_PathBuffer.clear();
				m_NameBuffer = "MyGame";
			}
			if (ImGui::MenuItem("Open Project..."))
			{
				m_PathPopup = "open_project";
				m_PathBuffer.clear();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("New Scene", "Ctrl+N"))
				m_Context->NewScene("Untitled");
			if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, m_Context->HasProject()))
				SaveSceneOrPrompt();
			if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S", false, m_Context->HasProject()))
			{
				m_PathPopup = "save_scene_as";
				m_PathBuffer = m_Context->GetScenePath().empty() ? "Scenes/NewScene.escene" : m_Context->GetScenePath();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Export Game...", nullptr, false, m_Context->HasProject()))
			{
				m_PathPopup = "export_game";
				m_PathBuffer = (m_Context->GetProject()->GetProjectDirectory().parent_path() / (m_Context->GetProject()->GetConfig().Name + "_Build")).string();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Exit"))
				Application::Get().Close();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit"))
		{
			if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_Context->CanUndo()))
				m_Context->Undo();
			if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_Context->CanRedo()))
				m_Context->Redo();
			ImGui::Separator();
			Entity selected = m_Context->GetSelectedEntity();
			if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, static_cast<bool>(selected)))
				m_Commands->Execute("duplicate_entity", { { "entity", std::to_string(static_cast<uint64_t>(selected.GetUUID())) } });
			if (ImGui::MenuItem("Delete", "Del", false, static_cast<bool>(selected)))
				m_Commands->Execute("destroy_entity", { { "entity", std::to_string(static_cast<uint64_t>(selected.GetUUID())) } });
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Entity"))
		{
			if (ImGui::MenuItem("Empty"))
				CreatePrimitive("Entity", nullptr);
			for (const char* primitive : { "Cube", "Sphere", "Plane", "Cylinder", "Capsule" })
			{
				if (ImGui::MenuItem(primitive))
					CreatePrimitive(primitive, (std::string("builtin://") + primitive).c_str());
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Camera"))
				m_Commands->Execute("create_entity", { { "name", "Camera" }, { "components", { { "Camera", Json::object() } } } });
			if (ImGui::MenuItem("Directional Light"))
				m_Commands->Execute("create_entity", { { "name", "Directional Light" }, { "components", { { "DirectionalLight", Json::object() },
					{ "Transform", { { "RotationDegrees", { -50, 30, 0 } } } } } } });
			if (ImGui::MenuItem("Point Light"))
				m_Commands->Execute("create_entity", { { "name", "Point Light" }, { "components", { { "PointLight", Json::object() } } } });
			if (ImGui::MenuItem("Spot Light"))
				m_Commands->Execute("create_entity", { { "name", "Spot Light" }, { "components", { { "SpotLight", Json::object() } } } });
			if (ImGui::MenuItem("Sky Light"))
				m_Commands->Execute("create_entity", { { "name", "Sky" }, { "components", { { "SkyLight", Json::object() } } } });
			ImGui::EndMenu();
		}

		ImGui::Separator();
		std::string title = m_Context->HasProject() ? m_Context->GetProject()->GetConfig().Name : std::string("No project");
		title += "  |  " + (m_Context->GetScenePath().empty() ? std::string("unsaved scene") : m_Context->GetScenePath());
		if (m_Context->IsSceneDirty())
			title += " *";
		ImGui::TextDisabled("%s", title.c_str());
		if (!m_StatusMessage.empty())
		{
			ImGui::Separator();
			ImGui::TextUnformatted(m_StatusMessage.c_str());
		}
		ImGui::EndMainMenuBar();
	}

	void EditorLayer::CreatePrimitive(const char* name, const char* mesh)
	{
		Json components = Json::object();
		if (mesh)
			components["MeshRenderer"] = { { "Mesh", mesh } };
		const Json result = m_Commands->Execute("create_entity", { { "name", name }, { "components", components } });
		m_Context->SetSelectedEntity(std::stoull(result["id"].get<std::string>()));
	}

	void EditorLayer::SaveSceneOrPrompt()
	{
		if (m_Context->GetScenePath().empty())
		{
			m_PathPopup = "save_scene_as";
			m_PathBuffer = "Scenes/NewScene.escene";
			return;
		}
		std::string error;
		m_StatusMessage = m_Context->SaveScene({}, &error) ? "Saved " + m_Context->GetScenePath() : "Save failed: " + error;
	}

	void EditorLayer::DrawToolbar()
	{
		ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar);
		const bool playing = m_Context->IsPlaying();
		if (ImGui::Button(playing ? "Stop" : "Play"))
			playing ? m_Context->Stop() : m_Context->Play();
		ImGui::SameLine();
		ImGui::BeginDisabled(!playing);
		if (ImGui::Button(m_Context->IsPaused() ? "Resume" : "Pause"))
			m_Context->SetPaused(!m_Context->IsPaused());
		ImGui::SameLine();
		if (ImGui::Button("Step"))
			m_Context->Step(1);
		ImGui::EndDisabled();

		ImGui::SameLine(0.0f, 30.0f);
		const char* gizmoNames[] = { "Select (Q)", "Move (W)", "Rotate (E)", "Scale (R)" };
		const int gizmoOps[] = { 0, ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE };
		for (int i = 0; i < 4; i++)
		{
			if (i > 0)
				ImGui::SameLine();
			const bool active = m_GizmoOperation == gizmoOps[i];
			if (active)
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			if (ImGui::Button(gizmoNames[i]))
				m_GizmoOperation = gizmoOps[i];
			if (active)
				ImGui::PopStyleColor();
		}
		ImGui::End();
	}

	void EditorLayer::DrawViewport()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin(m_Context->IsPlaying() ? "Viewport (Playing)###Viewport" : "Viewport###Viewport");
		m_ViewportFocused = ImGui::IsWindowFocused();
		m_ViewportHovered = ImGui::IsWindowHovered();

		const ImVec2 available = ImGui::GetContentRegionAvail();
		m_ViewportSize = { std::max(available.x, 1.0f), std::max(available.y, 1.0f) };
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		m_ViewportOrigin = { origin.x, origin.y };

		SceneRenderer& renderer = m_Context->GetViewportRenderer();
		ImGui::Image(ImGuiLayer::ToTextureID(renderer.GetFinalImage()), ImVec2(static_cast<float>(renderer.GetViewportWidth()),
			static_cast<float>(renderer.GetViewportHeight())));

		// Drag & drop assets into the scene.
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(c_AssetPayload))
			{
				const std::string path(static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize));
				const std::string ext = Lower(std::filesystem::path(path).extension().string());
				try
				{
					if (ext == ".escene")
						m_Commands->Execute("open_scene", { { "path", path } });
					else if (ext == ".eprefab")
						m_Commands->Execute("instantiate_prefab", { { "path", path } });
					else if (ext == ".gltf" || ext == ".glb")
						m_Commands->Execute("create_entity", { { "name", std::filesystem::path(path).stem().string() },
							{ "components", { { "MeshRenderer", { { "Mesh", path } } } } } });
					else if (ext == ".hdr")
						m_Commands->Execute("create_entity", { { "name", "Sky" }, { "components", { { "SkyLight", { { "EnvironmentMap", path } } } } } });
					else if (ext == ".ttf" || ext == ".otf")
						m_Commands->Execute("create_entity", { { "name", "Text" },
							{ "components", { { "UIText", { { "Text", "Text" }, { "Font", path } } } } } });
				}
				catch (const CommandError& error)
				{
					m_StatusMessage = error.what();
				}
			}
			ImGui::EndDragDropTarget();
		}

		// Gizmo for the selected entity (edit mode).
		Entity selected = m_Context->GetSelectedEntity();
		bool gizmoHovered = false;
		if (!m_Context->IsPlaying())
		{
			EditorCamera& camera = m_Context->GetEditorCamera();
			glm::mat4 view = camera.GetViewMatrix();
			glm::mat4 projection = camera.GetProjection();
			ImGuizmo::SetOrthographic(false);
			ImGuizmo::SetDrawlist();
			ImGuizmo::SetRect(origin.x, origin.y, m_ViewportSize.x, m_ViewportSize.y);
			glm::mat4 identity(1.0f);
			ImGuizmo::DrawGrid(glm::value_ptr(view), glm::value_ptr(projection), glm::value_ptr(identity), 20.0f);

			if (selected && m_GizmoOperation != 0)
			{
				Scene& scene = *m_Context->GetActiveScene();
				glm::mat4 world = scene.GetWorldTransform(selected);
				const bool snap = Input::IsKeyPressed(KeyCode::LeftControl);
				const float snapValue = m_GizmoOperation == ImGuizmo::ROTATE ? 15.0f : 0.5f;
				const float snapValues[3] = { snapValue, snapValue, snapValue };
				ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), static_cast<ImGuizmo::OPERATION>(m_GizmoOperation),
					ImGuizmo::LOCAL, glm::value_ptr(world), nullptr, snap ? snapValues : nullptr);
				const bool using_ = ImGuizmo::IsUsing();
				if (using_ && !m_GizmoWasUsing)
					m_Context->RecordUndo("Transform");
				if (using_)
				{
					scene.SetWorldTransform(selected, world);
					m_Context->MarkDirty();
				}
				m_GizmoWasUsing = using_;
				gizmoHovered = ImGuizmo::IsOver();
			}
		}

		// Click to select.
		if (m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gizmoHovered && !ImGuizmo::IsUsing() &&
			!Input::IsKeyPressed(KeyCode::LeftAlt))
		{
			const ImVec2 mouse = ImGui::GetMousePos();
			const float x = mouse.x - origin.x;
			const float y = mouse.y - origin.y;
			if (x >= 0.0f && y >= 0.0f && x < m_ViewportSize.x && y < m_ViewportSize.y)
				m_Context->SetSelectedEntity(m_Context->PickEntity(static_cast<uint32_t>(x), static_cast<uint32_t>(y), true));
		}

		ImGui::End();
		ImGui::PopStyleVar();
	}

	void EditorLayer::DrawHierarchyNode(Entity entity)
	{
		Scene& scene = *m_Context->GetActiveScene();
		const std::vector<Entity> children = scene.GetChildren(entity);
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (children.empty())
			flags |= ImGuiTreeNodeFlags_Leaf;
		if (m_Context->GetSelectedEntity() == entity)
			flags |= ImGuiTreeNodeFlags_Selected;

		const uint64_t id = entity.GetUUID();
		const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(id)), flags, "%s", entity.GetName().c_str());
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
			m_Context->SetSelectedEntity(id);

		if (ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload(c_EntityPayload, &id, sizeof(id));
			ImGui::TextUnformatted(entity.GetName().c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(c_EntityPayload))
			{
				uint64_t child = 0;
				std::memcpy(&child, payload->Data, sizeof(child));
				try
				{
					m_Commands->Execute("set_parent", { { "entity", std::to_string(child) }, { "parent", std::to_string(id) } });
				}
				catch (const CommandError& error)
				{
					m_StatusMessage = error.what();
				}
			}
			ImGui::EndDragDropTarget();
		}

		bool destroyed = false;
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Duplicate"))
				m_Commands->Execute("duplicate_entity", { { "entity", std::to_string(id) } });
			if (ImGui::MenuItem("Save as Prefab", nullptr, false, m_Context->HasProject()))
				m_Commands->Execute("create_prefab", { { "entity", std::to_string(id) }, { "path", "Prefabs/" + entity.GetName() + ".eprefab" } });
			if (ImGui::MenuItem("Delete"))
			{
				m_Commands->Execute("destroy_entity", { { "entity", std::to_string(id) } });
				destroyed = true;
			}
			ImGui::EndPopup();
		}

		if (open)
		{
			if (!destroyed)
			{
				for (Entity child : children)
				{
					if (child.IsValid())
						DrawHierarchyNode(child);
				}
			}
			ImGui::TreePop();
		}
	}

	void EditorLayer::DrawHierarchy()
	{
		ImGui::Begin("Hierarchy");
		Scene& scene = *m_Context->GetActiveScene();
		for (Entity entity : scene.GetRootEntities())
		{
			if (entity.IsValid())
				DrawHierarchyNode(entity);
		}

		// Drop on empty space: un-parent.
		ImGui::Dummy(ImGui::GetContentRegionAvail());
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(c_EntityPayload))
			{
				uint64_t child = 0;
				std::memcpy(&child, payload->Data, sizeof(child));
				m_Commands->Execute("set_parent", { { "entity", std::to_string(child) }, { "parent", nullptr } });
			}
			ImGui::EndDragDropTarget();
		}
		if (ImGui::IsItemClicked())
			m_Context->ClearSelection();
		if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			if (ImGui::MenuItem("Create Empty"))
				CreatePrimitive("Entity", nullptr);
			if (ImGui::MenuItem("Create Cube"))
				CreatePrimitive("Cube", "builtin://Cube");
			ImGui::EndPopup();
		}
		ImGui::End();
	}

	void EditorLayer::DrawInspector()
	{
		ImGui::Begin("Inspector");
		Entity entity = m_Context->GetSelectedEntity();
		if (!entity)
		{
			ImGui::TextDisabled("Select an entity to edit its components.");
			ImGui::End();
			return;
		}

		ImGui::TextDisabled("ID %llu", static_cast<unsigned long long>(static_cast<uint64_t>(entity.GetUUID())));
		std::string name = entity.GetName();
		if (ImGui::InputText("Name", &name, ImGuiInputTextFlags_EnterReturnsTrue))
		{
			m_Context->RecordUndo("Rename");
			entity.GetComponent<TagComponent>().Tag = name;
		}

		for (const ComponentInfo& info : ComponentRegistry::GetAll())
		{
			if (info.Name == "Tag" || info.Name == "Relationship" || !info.Has(entity))
				continue;

			ImGui::PushID(info.Name.c_str());
			const bool open = ImGui::CollapsingHeader(info.Name.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
			bool removed = false;
			if (info.UserAddable)
			{
				ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
				if (ImGui::SmallButton("x"))
				{
					m_Context->RecordUndo("Remove " + info.Name);
					info.Remove(entity);
					removed = true;
				}
			}
			if (open && !removed)
			{
				YAML::Node data = info.Serialize(entity);
				// Show rotation in degrees.
				if (info.Name == "Transform")
				{
					const glm::vec3 radians = data["Rotation"].as<glm::vec3>();
					YAML::Node ordered(YAML::NodeType::Map);
					ordered["Translation"] = data["Translation"];
					ordered["RotationDegrees"] = glm::degrees(radians);
					ordered["Scale"] = data["Scale"];
					data = ordered;
				}
				YAML::Node patch(YAML::NodeType::Map);
				bool activated = false;
				bool changed = false;
				for (const auto& item : data)
					changed |= DrawField(item.first.as<std::string>(), item.second, patch, activated);
				if (activated)
					m_Context->RecordUndo("Edit " + info.Name);
				if (changed)
				{
					info.Deserialize(entity, patch);
					m_Context->MarkDirty();
				}
			}
			ImGui::PopID();
		}

		ImGui::Separator();
		if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f)))
			ImGui::OpenPopup("AddComponent");
		if (ImGui::BeginPopup("AddComponent"))
		{
			for (const ComponentInfo& info : ComponentRegistry::GetAll())
			{
				if (!info.UserAddable || info.Has(entity))
					continue;
				if (ImGui::MenuItem(info.Name.c_str()))
				{
					m_Context->RecordUndo("Add " + info.Name);
					info.Add(entity);
				}
			}
			ImGui::EndPopup();
		}
		ImGui::End();
	}

	void EditorLayer::DrawAssetBrowser()
	{
		ImGui::Begin("Assets");
		if (!m_Context->HasProject())
		{
			ImGui::TextDisabled("Open or create a project (File menu).");
			ImGui::End();
			return;
		}
		const std::filesystem::path root = m_Context->GetProject()->GetAssetDirectory();
		if (m_BrowserDirectory.empty() || m_BrowserDirectory.lexically_relative(root).string().rfind("..", 0) == 0)
			m_BrowserDirectory = root;

		const std::string relativeDir = m_BrowserDirectory.lexically_relative(root).generic_string();
		ImGui::TextDisabled("Assets/%s", relativeDir == "." ? "" : relativeDir.c_str());
		if (m_BrowserDirectory != root)
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("Up"))
				m_BrowserDirectory = m_BrowserDirectory.parent_path();
		}
		ImGui::Separator();

		std::error_code ec;
		std::vector<std::filesystem::directory_entry> entries;
		for (const auto& entry : std::filesystem::directory_iterator(m_BrowserDirectory, ec))
			entries.push_back(entry);
		std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b)
		{
			if (a.is_directory() != b.is_directory())
				return a.is_directory();
			return a.path().filename() < b.path().filename();
		});

		for (const auto& entry : entries)
		{
			const std::string filename = entry.path().filename().string();
			const std::string label = (entry.is_directory() ? "[dir] " : "      ") + filename;
			ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
			const std::string assetPath = entry.path().lexically_relative(root).generic_string();
			if (!entry.is_directory() && ImGui::BeginDragDropSource())
			{
				ImGui::SetDragDropPayload(c_AssetPayload, assetPath.data(), assetPath.size());
				ImGui::TextUnformatted(assetPath.c_str());
				ImGui::EndDragDropSource();
			}
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				if (entry.is_directory())
					m_BrowserDirectory = entry.path();
				else if (Lower(entry.path().extension().string()) == ".escene")
				{
					std::string error;
					if (!m_Context->OpenScene(assetPath, &error))
						m_StatusMessage = "Open failed: " + error;
				}
			}
		}
		ImGui::End();
	}

	void EditorLayer::DrawConsole()
	{
		ImGui::Begin("Console");
		if (ImGui::SmallButton("Clear"))
			m_ConsoleSequence = Log::GetHistory().GetLatestSequence();
		ImGui::SameLine();
		ImGui::Checkbox("Auto-scroll", &m_ConsoleAutoScroll);
		ImGui::SameLine();
		const char* levels[] = { "All", "Info", "Warnings", "Errors" };
		ImGui::SetNextItemWidth(100.0f);
		ImGui::Combo("##level", &m_ConsoleMinLevel, levels, 4);
		ImGui::Separator();

		ImGui::BeginChild("ConsoleScroll", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
		for (const LogMessage& message : Log::GetHistory().GetSince(m_ConsoleSequence))
		{
			if (static_cast<int>(message.Level) < m_ConsoleMinLevel)
				continue;
			ImVec4 color(0.8f, 0.8f, 0.8f, 1.0f);
			if (message.Level == LogLevel::Warn)
				color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
			else if (message.Level >= LogLevel::Error)
				color = ImVec4(1.0f, 0.4f, 0.35f, 1.0f);
			else if (message.Level == LogLevel::Trace)
				color = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::TextUnformatted((message.Logger + ": " + message.Text).c_str());
			ImGui::PopStyleColor();
		}
		if (m_ConsoleAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f)
			ImGui::SetScrollHereY(1.0f);
		ImGui::EndChild();
		ImGui::End();
	}

	void EditorLayer::DrawRenderPanel()
	{
		ImGui::Begin("Rendering");
		const RenderStats& stats = m_Context->GetViewportRenderer().GetStats();
		ImGui::Text("Frame: %.2f ms (%.0f FPS)", Application::Get().GetLastFrameTime() * 1000.0f,
			1.0f / std::max(Application::Get().GetLastFrameTime(), 1e-4f));
		ImGui::Text("GPU: %s", m_Context->GetGraphicsDevice().GetAdapterName().c_str());
		ImGui::Text("Draw calls: %u (+%u shadow), triangles: %u, culled: %u, UI elements: %u", stats.DrawCalls, stats.ShadowDrawCalls,
			stats.Triangles, stats.CulledObjects, stats.UIElements);
		ImGui::Separator();
		RenderSettings& settings = m_Context->GetViewportRenderer().GetSettings();
		ImGui::Checkbox("Shadows", &settings.ShadowsEnabled);
		ImGui::Checkbox("SSAO", &settings.SSAOEnabled);
		ImGui::SliderFloat("Exposure", &settings.Exposure, 0.05f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
		ImGui::SliderFloat("SSAO radius", &settings.SSAORadius, 0.05f, 3.0f);
		ImGui::SliderFloat("Shadow distance", &settings.ShadowDistance, 5.0f, 300.0f);
		const char* tonemappers[] = { "ACES", "Reinhard", "None" };
		int tonemapper = static_cast<int>(settings.Tonemapper);
		if (ImGui::Combo("Tonemapper", &tonemapper, tonemappers, 3))
			settings.Tonemapper = static_cast<TonemapOperator>(tonemapper);
		ImGui::Separator();
		if (m_Server.IsRunning())
			ImGui::Text("AI server: 127.0.0.1:%u", static_cast<unsigned>(m_Server.GetPort()));
		else
			ImGui::TextDisabled("AI server: off (start with --ai-server)");
		ImGui::End();
	}

	void EditorLayer::DrawPathPopups()
	{
		if (m_PathPopup.empty())
			return;
		const char* titles[][2] = {
			{ "open_project", "Open Project" }, { "new_project", "New Project" },
			{ "save_scene_as", "Save Scene As" }, { "export_game", "Export Game" } };
		const char* title = "Path";
		for (const auto& entry : titles)
		{
			if (m_PathPopup == entry[0])
				title = entry[1];
		}
		ImGui::OpenPopup(title);
		bool open = true;
		if (ImGui::BeginPopupModal(title, &open, ImGuiWindowFlags_AlwaysAutoResize))
		{
			if (m_PathPopup == "new_project")
			{
				ImGui::InputText("Name", &m_NameBuffer);
				ImGui::InputText("Directory", &m_PathBuffer);
			}
			else if (m_PathPopup == "save_scene_as")
			{
				ImGui::InputText("Asset path", &m_PathBuffer);
			}
			else
			{
				ImGui::InputText("Path", &m_PathBuffer);
			}

			if (ImGui::Button("OK", ImVec2(120.0f, 0.0f)))
			{
				std::string error;
				try
				{
					if (m_PathPopup == "open_project")
						m_Commands->Execute("open_project", { { "path", m_PathBuffer } });
					else if (m_PathPopup == "new_project")
						m_Commands->Execute("new_project", { { "path", m_PathBuffer }, { "name", m_NameBuffer } });
					else if (m_PathPopup == "save_scene_as")
						m_Commands->Execute("save_scene", { { "path", m_PathBuffer } });
					else if (m_PathPopup == "export_game")
					{
						const Json result = m_Commands->Execute("export_game", { { "output", m_PathBuffer } });
						m_StatusMessage = "Exported " + result["executable"].get<std::string>();
					}
					if (m_Context->HasProject())
						m_BrowserDirectory = m_Context->GetProject()->GetAssetDirectory();
					m_PathPopup.clear();
					ImGui::CloseCurrentPopup();
				}
				catch (const CommandError& e)
				{
					m_StatusMessage = e.what();
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
			{
				m_PathPopup.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		if (!open)
			m_PathPopup.clear();
	}

	bool EditorLayer::HandleShortcuts(KeyPressedEvent& event)
	{
		if (event.IsRepeat())
			return false;
		const bool control = Input::IsKeyPressed(KeyCode::LeftControl) || Input::IsKeyPressed(KeyCode::RightControl);
		const bool shift = Input::IsKeyPressed(KeyCode::LeftShift) || Input::IsKeyPressed(KeyCode::RightShift);
		Entity selected = m_Context->GetSelectedEntity();
		try
		{
			switch (event.GetKeyCode())
			{
				case KeyCode::S:
					if (control && shift && m_Context->HasProject())
					{
						m_PathPopup = "save_scene_as";
						m_PathBuffer = m_Context->GetScenePath();
						return true;
					}
					if (control && m_Context->HasProject())
					{
						SaveSceneOrPrompt();
						return true;
					}
					break;
				case KeyCode::Z:
					if (control)
						return m_Context->Undo();
					break;
				case KeyCode::Y:
					if (control)
						return m_Context->Redo();
					break;
				case KeyCode::D:
					if (control && selected)
					{
						m_Commands->Execute("duplicate_entity", { { "entity", std::to_string(static_cast<uint64_t>(selected.GetUUID())) } });
						return true;
					}
					break;
				case KeyCode::N:
					if (control)
					{
						m_Context->NewScene("Untitled");
						return true;
					}
					break;
				case KeyCode::P:
					if (control)
					{
						m_Context->IsPlaying() ? m_Context->Stop() : m_Context->Play();
						return true;
					}
					break;
				case KeyCode::Delete:
					if (selected && m_ViewportHovered)
					{
						m_Commands->Execute("destroy_entity", { { "entity", std::to_string(static_cast<uint64_t>(selected.GetUUID())) } });
						return true;
					}
					break;
				case KeyCode::F:
					if (selected && m_ViewportHovered)
					{
						m_Commands->Execute("focus_entity", { { "entity", std::to_string(static_cast<uint64_t>(selected.GetUUID())) } });
						return true;
					}
					break;
				case KeyCode::Q: if (m_ViewportHovered && !Input::IsMouseButtonPressed(MouseButton::Right)) m_GizmoOperation = 0; break;
				case KeyCode::W: if (m_ViewportHovered && !Input::IsMouseButtonPressed(MouseButton::Right)) m_GizmoOperation = ImGuizmo::TRANSLATE; break;
				case KeyCode::E: if (m_ViewportHovered && !Input::IsMouseButtonPressed(MouseButton::Right)) m_GizmoOperation = ImGuizmo::ROTATE; break;
				case KeyCode::R: if (m_ViewportHovered && !Input::IsMouseButtonPressed(MouseButton::Right)) m_GizmoOperation = ImGuizmo::SCALE; break;
				default:
					break;
			}
		}
		catch (const CommandError& error)
		{
			m_StatusMessage = error.what();
		}
		return false;
	}

}
