#include "EditorContext.h"

#include "JsonUtils.h"

#include "Ember/Core/FileSystem.h"
#include "Ember/Core/Input.h"
#include "Ember/Core/Log.h"
#include "Ember/Renderer/GraphicsDevice.h"
#include "Ember/Scene/SceneSerializer.h"

namespace Ember {

	EditorContext::EditorContext(GraphicsDevice& device)
		: m_Device(device)
	{
		m_RenderAssets = CreateScope<RenderAssets>(device.GetDevice(), RenderSettings{});
		m_ViewportRenderer = CreateScope<SceneRenderer>(*m_RenderAssets);
		NewScene("Untitled");
		m_EditorCamera.LookAt({ 6.0f, 4.0f, 8.0f }, { 0.0f, 0.5f, 0.0f });
	}

	EditorContext::~EditorContext()
	{
		if (IsPlaying())
			Stop();
		m_Device.GetDevice()->waitForIdle();
		m_CaptureRenderer.reset();
		m_ViewportRenderer.reset();
		m_RuntimeScene.reset();
		m_EditorScene.reset();
		m_RenderAssets.reset();
	}

	Project& EditorContext::RequireProject() const
	{
		if (!m_Project)
			throw CommandError("no project is open (use new_project or open_project)");
		return *m_Project;
	}

	void EditorContext::ConfigureScene(const Ref<Scene>& scene)
	{
		if (m_Project)
			scene->SetAssetDirectory(m_Project->GetAssetDirectory());
		scene->OnViewportResize(m_ViewportWidth, m_ViewportHeight);
	}

	bool EditorContext::NewProject(const std::filesystem::path& directory, const std::string& name, std::string* outError)
	{
		if (IsPlaying())
			Stop();
		Ref<Project> project = Project::Create(directory, name, outError);
		if (!project)
			return false;
		return OpenProject(project->GetProjectFile(), outError);
	}

	bool EditorContext::OpenProject(const std::filesystem::path& path, std::string* outError)
	{
		if (IsPlaying())
			Stop();
		Ref<Project> project = Project::Load(path, outError);
		if (!project)
			return false;
		m_Project = project;

		// Choose render quality for this device and point the asset caches at the project.
		const RenderSettings settings = m_Project->GetRenderSettings(m_Device.GetAdapterName());
		m_CaptureRenderer.reset();
		m_ViewportRenderer.reset();
		m_RenderAssets = CreateScope<RenderAssets>(m_Device.GetDevice(), settings);
		m_RenderAssets->SetAssetDirectory(m_Project->GetAssetDirectory());
		m_ViewportRenderer = CreateScope<SceneRenderer>(*m_RenderAssets);
		m_ViewportRenderer->SetViewportSize(m_ViewportWidth, m_ViewportHeight);

		const std::string& startScene = m_Project->GetConfig().StartScene;
		if (!startScene.empty() && FileSystem::Exists(m_Project->GetAssetPath(startScene)))
		{
			if (!OpenScene(startScene, outError))
				NewScene("Untitled");
		}
		else
		{
			NewScene("Untitled");
		}
		EMBER_CORE_INFO("Opened project '{}' ({})", m_Project->GetConfig().Name, m_Project->GetProjectDirectory().string());
		return true;
	}

	bool EditorContext::SaveProject()
	{
		return m_Project && m_Project->Save();
	}

	void EditorContext::NewScene(const std::string& name)
	{
		if (IsPlaying())
			Stop();
		m_EditorScene = CreateRef<Scene>(name.empty() ? "Untitled" : name);
		ConfigureScene(m_EditorScene);
		m_ScenePath.clear();
		m_Dirty = false;
		m_Selection = 0;
		ResetHistory();
	}

	bool EditorContext::OpenScene(const std::string& assetPath, std::string* outError)
	{
		Project& project = RequireProject();
		if (!FileSystem::IsSafeRelativePath(assetPath))
		{
			if (outError)
				*outError = "scene path must be relative to the asset directory";
			return false;
		}
		if (IsPlaying())
			Stop();
		Ref<Scene> scene = CreateRef<Scene>();
		if (!SceneSerializer(scene).Deserialize(project.GetAssetPath(assetPath), outError))
			return false;
		m_EditorScene = scene;
		ConfigureScene(m_EditorScene);
		m_ScenePath = assetPath;
		m_Dirty = false;
		m_Selection = 0;
		ResetHistory();
		return true;
	}

	bool EditorContext::SaveScene(const std::string& assetPath, std::string* outError)
	{
		Project& project = RequireProject();
		const std::string path = assetPath.empty() ? m_ScenePath : assetPath;
		if (path.empty())
		{
			if (outError)
				*outError = "the scene has no file yet; provide a path";
			return false;
		}
		if (!FileSystem::IsSafeRelativePath(path))
		{
			if (outError)
				*outError = "scene path must be relative to the asset directory";
			return false;
		}
		if (!SceneSerializer(m_EditorScene).Serialize(project.GetAssetPath(path)))
		{
			if (outError)
				*outError = "failed to write the scene file";
			return false;
		}
		m_ScenePath = path;
		m_Dirty = false;
		// The first saved scene becomes the start scene if none is set.
		if (project.GetConfig().StartScene.empty())
		{
			project.GetConfig().StartScene = path;
			project.Save();
		}
		return true;
	}

	void EditorContext::Play()
	{
		if (IsPlaying())
			return;
		Input::Reset();
		m_RuntimeScene = Scene::Copy(m_EditorScene);
		ConfigureScene(m_RuntimeScene);
		m_RuntimeScene->OnRuntimeStart();
		EMBER_CORE_INFO("Play mode started");
	}

	void EditorContext::Stop()
	{
		if (!IsPlaying())
			return;
		m_RuntimeScene->OnRuntimeStop();
		m_RuntimeScene.reset();
		Input::Reset();
		EMBER_CORE_INFO("Play mode stopped");
	}

	void EditorContext::SetPaused(bool paused)
	{
		if (m_RuntimeScene)
			m_RuntimeScene->SetPaused(paused);
	}

	void EditorContext::Step(int frames)
	{
		if (m_RuntimeScene)
		{
			m_RuntimeScene->SetPaused(true);
			m_RuntimeScene->Step(frames);
		}
	}

	void EditorContext::OnUpdate(Timestep ts, bool viewportInputEnabled)
	{
		if (m_RuntimeScene)
		{
			m_RuntimeScene->OnUpdateRuntime(ts);
		}
		else
		{
			m_EditorCamera.OnUpdate(ts, viewportInputEnabled);
			m_EditorScene->OnUpdateEditor(ts);
		}
	}

	void EditorContext::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0)
			return;
		m_ViewportWidth = width;
		m_ViewportHeight = height;
		m_ViewportRenderer->SetViewportSize(width, height);
		m_EditorCamera.SetViewportSize(width, height);
		m_EditorScene->OnViewportResize(width, height);
		if (m_RuntimeScene)
			m_RuntimeScene->OnViewportResize(width, height);
	}

	void EditorContext::RenderViewport()
	{
		Scene& scene = *GetActiveScene();
		RenderCamera camera = m_EditorCamera.GetRenderCamera();
		if (IsPlaying())
		{
			if (std::optional<RenderCamera> gameCamera = SceneRenderer::GetPrimaryCamera(scene))
				camera = *gameCamera;
		}
		m_ViewportRenderer->Render(scene, camera);
	}

	std::optional<Image> EditorContext::Capture(uint32_t width, uint32_t height, bool useGameCamera, std::string* outError)
	{
		if (width == 0 || height == 0 || width > 8192 || height > 8192)
		{
			if (outError)
				*outError = "capture size must be between 1 and 8192";
			return std::nullopt;
		}
		Scene& scene = *GetActiveScene();
		RenderCamera camera;
		if (useGameCamera)
		{
			// Aspect ratio of scene cameras follows the capture size.
			scene.OnViewportResize(width, height);
			std::optional<RenderCamera> gameCamera = SceneRenderer::GetPrimaryCamera(scene);
			scene.OnViewportResize(m_ViewportWidth, m_ViewportHeight);
			if (!gameCamera)
			{
				if (outError)
					*outError = "the scene has no primary camera";
				return std::nullopt;
			}
			camera = *gameCamera;
		}
		else
		{
			EditorCamera editorCamera = m_EditorCamera;
			editorCamera.SetViewportSize(width, height);
			camera = editorCamera.GetRenderCamera();
		}

		if (!m_CaptureRenderer)
			m_CaptureRenderer = CreateScope<SceneRenderer>(*m_RenderAssets);
		m_CaptureRenderer->GetSettings() = m_ViewportRenderer->GetSettings();
		m_CaptureRenderer->SetViewportSize(width, height);
		m_CaptureRenderer->Render(scene, camera);
		std::optional<Image> image = m_CaptureRenderer->CaptureImage();
		if (!image && outError)
			*outError = "GPU readback failed";
		return image;
	}

	bool EditorContext::CaptureToFile(const std::filesystem::path& path, uint32_t width, uint32_t height, bool useGameCamera, std::string* outError)
	{
		std::optional<Image> image = Capture(width, height, useGameCamera, outError);
		if (!image)
			return false;
		if (!image->SavePNG(path))
		{
			if (outError)
				*outError = "failed to write '" + path.string() + "'";
			return false;
		}
		return true;
	}

	RenderStats EditorContext::GetCaptureStats() const
	{
		return m_CaptureRenderer ? m_CaptureRenderer->GetStats() : RenderStats{};
	}

	UUID EditorContext::PickEntity(uint32_t x, uint32_t y, bool fromViewport)
	{
		if (fromViewport)
			return m_ViewportRenderer->PickEntity(x, y);
		return m_CaptureRenderer ? m_CaptureRenderer->PickEntity(x, y) : UUID(0);
	}

	Entity EditorContext::GetSelectedEntity()
	{
		return GetActiveScene()->GetEntityByUUID(m_Selection);
	}

	void EditorContext::RecordUndo(const std::string& description)
	{
		if (IsPlaying())
			return; // runtime changes are discarded on stop anyway
		m_UndoStack.push_back({ description, SceneSerializer(m_EditorScene).SerializeToString(), m_Selection });
		if (m_UndoStack.size() > c_MaxUndo)
			m_UndoStack.pop_front();
		m_RedoStack.clear();
		m_Dirty = true;
	}

	void EditorContext::ApplySnapshot(const Snapshot& snapshot)
	{
		Ref<Scene> scene = CreateRef<Scene>();
		if (!SceneSerializer(scene).DeserializeFromString(snapshot.SceneData))
		{
			EMBER_CORE_ERROR("Undo: failed to restore snapshot '{}'", snapshot.Description);
			return;
		}
		m_EditorScene = scene;
		ConfigureScene(m_EditorScene);
		m_Selection = snapshot.Selection;
		m_Dirty = true;
	}

	bool EditorContext::Undo()
	{
		if (IsPlaying() || m_UndoStack.empty())
			return false;
		Snapshot snapshot = std::move(m_UndoStack.back());
		m_UndoStack.pop_back();
		m_RedoStack.push_back({ snapshot.Description, SceneSerializer(m_EditorScene).SerializeToString(), m_Selection });
		ApplySnapshot(snapshot);
		return true;
	}

	bool EditorContext::Redo()
	{
		if (IsPlaying() || m_RedoStack.empty())
			return false;
		Snapshot snapshot = std::move(m_RedoStack.back());
		m_RedoStack.pop_back();
		m_UndoStack.push_back({ snapshot.Description, SceneSerializer(m_EditorScene).SerializeToString(), m_Selection });
		ApplySnapshot(snapshot);
		return true;
	}

	const std::string& EditorContext::GetUndoDescription() const
	{
		static const std::string s_Empty;
		return m_UndoStack.empty() ? s_Empty : m_UndoStack.back().Description;
	}

	void EditorContext::ResetHistory()
	{
		m_UndoStack.clear();
		m_RedoStack.clear();
	}

}
