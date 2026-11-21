#pragma once

#include "AIServer.h"
#include "EditorCommands.h"
#include "EditorContext.h"

#include "Ember/Core/Layer.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Ember {

	struct EditorOptions
	{
		std::filesystem::path ProjectPath;
		std::string ScenePath;              // scene to open after the project (asset path)
		bool Headless = false;
		std::optional<uint16_t> AIServerPort; // start the AI control server
		std::filesystem::path BatchFile;    // execute JSON commands from a file, then exit
		bool RunTests = false;              // play the test scene and verify all script tests pass
		int TestFrames = 600;
		std::filesystem::path ScreenshotPath; // with RunTests: capture the final frame
	};

	class EditorLayer : public Layer
	{
	public:
		explicit EditorLayer(const EditorOptions& options);
		~EditorLayer() override;

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(Timestep ts) override;
		void OnRender() override;
		void OnImGuiRender() override;
		void OnEvent(Event& event) override;

		int GetExitCode() const { return m_ExitCode; }

	private:
		void RunBatch();
		void RunAutomatedTests();
		std::string DispatchFromNetwork(const std::string& line);

		// UI (EditorUI.cpp)
		void DrawDockspace();
		void DrawMenuBar();
		void DrawToolbar();
		void DrawViewport();
		void DrawHierarchy();
		void DrawHierarchyNode(Entity entity);
		void DrawInspector();
		void DrawAssetBrowser();
		void DrawConsole();
		void DrawRenderPanel();
		void DrawPathPopups();
		bool HandleShortcuts(KeyPressedEvent& event);
		void CreatePrimitive(const char* name, const char* mesh);
		void SaveSceneOrPrompt();

	private:
		EditorOptions m_Options;
		Scope<EditorContext> m_Context;
		Scope<EditorCommands> m_Commands;
		AIServer m_Server;
		int m_ExitCode = 0;
		bool m_StartupDone = false;

		// UI state
		bool m_ViewportHovered = false;
		bool m_ViewportFocused = false;
		glm::vec2 m_ViewportSize{ 1280.0f, 720.0f };
		glm::vec2 m_ViewportOrigin{ 0.0f };
		int m_GizmoOperation = 0; // ImGuizmo::OPERATION; 0 = none
		bool m_GizmoWasUsing = false;
		std::filesystem::path m_BrowserDirectory;
		uint64_t m_ConsoleSequence = 0;
		bool m_ConsoleAutoScroll = true;
		int m_ConsoleMinLevel = 0;
		std::string m_PathPopup;  // which path prompt is open ("open_project", "new_project", "save_scene_as", ...)
		std::string m_PathBuffer;
		std::string m_NameBuffer;
		std::string m_StatusMessage;
		bool m_InspectorEditActive = false;
	};

}
