#pragma once

#include "EditorCamera.h"

#include "Ember/Core/Base.h"
#include "Ember/Core/Timestep.h"
#include "Ember/Core/UUID.h"
#include "Ember/Project/Project.h"
#include "Ember/Renderer/RenderAssets.h"
#include "Ember/Renderer/SceneRenderer.h"
#include "Ember/Scene/Entity.h"
#include "Ember/Scene/Scene.h"

#include <deque>
#include <filesystem>
#include <optional>
#include <string>

namespace Ember {

	class GraphicsDevice;

	// UI-independent editor state and operations: project, edited scene, play mode, selection, undo/redo and
	// rendering. Both the ImGui editor and the AI control interface drive the editor exclusively through this
	// class (and EditorCommands on top of it), so they always behave identically.
	class EditorContext
	{
	public:
		explicit EditorContext(GraphicsDevice& device);
		~EditorContext();

		EditorContext(const EditorContext&) = delete;
		EditorContext& operator=(const EditorContext&) = delete;

		// --- Project ---------------------------------------------------------------------------------
		bool NewProject(const std::filesystem::path& directory, const std::string& name, std::string* outError = nullptr);
		bool OpenProject(const std::filesystem::path& path, std::string* outError = nullptr);
		bool SaveProject();
		const Ref<Project>& GetProject() const { return m_Project; }
		bool HasProject() const { return m_Project != nullptr; }
		// Throws CommandError if no project is open.
		Project& RequireProject() const;

		// --- Scenes ----------------------------------------------------------------------------------
		void NewScene(const std::string& name);
		bool OpenScene(const std::string& assetPath, std::string* outError = nullptr);
		// Saves to assetPath (or the current scene path if empty). Returns false if there is no path.
		bool SaveScene(const std::string& assetPath = {}, std::string* outError = nullptr);
		const std::string& GetScenePath() const { return m_ScenePath; }
		bool IsSceneDirty() const { return m_Dirty; }
		void MarkDirty() { m_Dirty = true; }

		const Ref<Scene>& GetEditorScene() const { return m_EditorScene; }
		// The scene currently shown/simulated: the runtime copy while playing, the edited scene otherwise.
		const Ref<Scene>& GetActiveScene() const { return m_RuntimeScene ? m_RuntimeScene : m_EditorScene; }

		// --- Play mode -------------------------------------------------------------------------------
		void Play();
		void Stop();
		void SetPaused(bool paused);
		void Step(int frames = 1);
		bool IsPlaying() const { return m_RuntimeScene != nullptr; }
		bool IsPaused() const { return m_RuntimeScene && m_RuntimeScene->IsPaused(); }

		// Advances the active scene. Call once per frame.
		void OnUpdate(Timestep ts, bool viewportInputEnabled);

		// --- Rendering -------------------------------------------------------------------------------
		RenderAssets& GetRenderAssets() { return *m_RenderAssets; }
		SceneRenderer& GetViewportRenderer() { return *m_ViewportRenderer; }
		EditorCamera& GetEditorCamera() { return m_EditorCamera; }
		void SetViewportSize(uint32_t width, uint32_t height);
		// Renders the active scene into the viewport renderer. In play mode the scene's primary camera is used
		// (falling back to the editor camera if the scene has none).
		void RenderViewport();
		// Renders the active scene at the given size and writes a PNG. useGameCamera selects the scene's primary
		// camera instead of the editor camera.
		bool CaptureToFile(const std::filesystem::path& path, uint32_t width, uint32_t height, bool useGameCamera, std::string* outError = nullptr);
		std::optional<Image> Capture(uint32_t width, uint32_t height, bool useGameCamera, std::string* outError = nullptr);
		// Entity at a pixel of the last capture (or of the viewport render).
		UUID PickEntity(uint32_t x, uint32_t y, bool fromViewport);

		// --- Selection -------------------------------------------------------------------------------
		Entity GetSelectedEntity();
		void SetSelectedEntity(UUID id) { m_Selection = id; }
		void ClearSelection() { m_Selection = 0; }

		// --- Undo / redo (edit mode only; snapshots of the whole edited scene) --------------------------
		// Call before modifying the edited scene.
		void RecordUndo(const std::string& description);
		bool Undo();
		bool Redo();
		bool CanUndo() const { return !m_UndoStack.empty(); }
		bool CanRedo() const { return !m_RedoStack.empty(); }
		const std::string& GetUndoDescription() const;

		GraphicsDevice& GetGraphicsDevice() { return m_Device; }

	private:
		struct Snapshot
		{
			std::string Description;
			std::string SceneData;
			UUID Selection = 0;
		};

		void ApplySnapshot(const Snapshot& snapshot);
		void ResetHistory();
		void ConfigureScene(const Ref<Scene>& scene);

	private:
		GraphicsDevice& m_Device;
		Ref<Project> m_Project;
		Ref<Scene> m_EditorScene;
		Ref<Scene> m_RuntimeScene;
		std::string m_ScenePath;
		bool m_Dirty = false;
		UUID m_Selection = 0;

		Scope<RenderAssets> m_RenderAssets;
		Scope<SceneRenderer> m_ViewportRenderer;
		Scope<SceneRenderer> m_CaptureRenderer;
		EditorCamera m_EditorCamera;
		uint32_t m_ViewportWidth = 1280;
		uint32_t m_ViewportHeight = 720;

		std::deque<Snapshot> m_UndoStack;
		std::deque<Snapshot> m_RedoStack;
		static constexpr size_t c_MaxUndo = 100;
	};

}
