#include "Ember/Project/Project.h"

#include "Ember/Core/FileSystem.h"
#include "Ember/Core/Log.h"
#include "Ember/Core/YAMLUtils.h"
#include "Ember/Scene/Scene.h"
#include "Ember/Scene/Entity.h"
#include "Ember/Scene/SceneSerializer.h"

#include <algorithm>
#include <cctype>

namespace Ember {

	namespace {

		void SetError(std::string* outError, const std::string& message)
		{
			EMBER_CORE_ERROR("Project: {}", message);
			if (outError)
				*outError = message;
		}

		std::string Lower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

	}

	Ref<Project> Project::Create(const std::filesystem::path& directory, const std::string& name, std::string* outError)
	{
		if (name.empty() || name.find_first_of("/\\:*?\"<>|") != std::string::npos)
		{
			SetError(outError, "invalid project name '" + name + "'");
			return nullptr;
		}

		std::error_code ec;
		std::filesystem::create_directories(directory, ec);
		const std::filesystem::path projectFile = directory / (name + c_Extension);
		if (FileSystem::Exists(projectFile))
		{
			SetError(outError, "a project already exists at '" + projectFile.string() + "'");
			return nullptr;
		}

		auto project = CreateRef<Project>();
		project->m_ProjectDirectory = std::filesystem::absolute(directory, ec).lexically_normal();
		project->m_ProjectFile = project->m_ProjectDirectory / (name + c_Extension);
		project->m_Config.Name = name;
		project->m_Config.StartScene = "Scenes/Main.escene";

		for (const char* sub : { "Scenes", "Scripts", "Prefabs", "Materials", "Models", "Textures", "Audio", "HDRI" })
			std::filesystem::create_directories(project->GetAssetDirectory() / sub, ec);

		// Starter scene: camera, sun and sky so a new project renders something sensible immediately.
		Ref<Scene> scene = CreateRef<Scene>("Main");
		Entity camera = scene->CreateEntity("Main Camera");
		camera.AddComponent<CameraComponent>();
		camera.AddComponent<AudioListenerComponent>();
		camera.GetComponent<TransformComponent>().Translation = { 0.0f, 2.0f, 8.0f };
		camera.GetComponent<TransformComponent>().Rotation = { glm::radians(-10.0f), 0.0f, 0.0f };
		Entity sun = scene->CreateEntity("Sun");
		sun.AddComponent<DirectionalLightComponent>();
		sun.GetComponent<TransformComponent>().Rotation = { glm::radians(-50.0f), glm::radians(30.0f), 0.0f };
		Entity sky = scene->CreateEntity("Sky");
		sky.AddComponent<SkyLightComponent>();
		if (!SceneSerializer(scene).Serialize(project->GetAssetPath(project->m_Config.StartScene)))
		{
			SetError(outError, "failed to write the start scene");
			return nullptr;
		}

		if (!project->Save())
		{
			SetError(outError, "failed to write the project file");
			return nullptr;
		}
		return project;
	}

	Ref<Project> Project::Load(const std::filesystem::path& path, std::string* outError)
	{
		std::error_code ec;
		std::filesystem::path projectFile = path;
		if (std::filesystem::is_directory(path, ec))
		{
			projectFile.clear();
			for (const auto& entry : std::filesystem::directory_iterator(path, ec))
			{
				if (entry.is_regular_file() && Lower(entry.path().extension().string()) == c_Extension)
				{
					if (!projectFile.empty())
					{
						SetError(outError, "multiple project files in '" + path.string() + "'");
						return nullptr;
					}
					projectFile = entry.path();
				}
			}
			if (projectFile.empty())
			{
				SetError(outError, "no " + std::string(c_Extension) + " file in '" + path.string() + "'");
				return nullptr;
			}
		}

		std::optional<std::string> text = FileSystem::ReadText(projectFile);
		if (!text)
		{
			SetError(outError, "cannot read '" + projectFile.string() + "'");
			return nullptr;
		}

		auto project = CreateRef<Project>();
		project->m_ProjectFile = std::filesystem::absolute(projectFile, ec).lexically_normal();
		project->m_ProjectDirectory = project->m_ProjectFile.parent_path();
		try
		{
			const YAML::Node root = YAML::Load(*text);
			const YAML::Node node = root["Project"];
			if (!node || !node.IsMap())
			{
				SetError(outError, "'" + projectFile.string() + "' is not a project file");
				return nullptr;
			}
			ProjectConfig& config = project->m_Config;
			ReadYAML(node, "Name", config.Name);
			std::string assetDirectory = config.AssetDirectory.string();
			ReadYAML(node, "AssetDirectory", assetDirectory);
			config.AssetDirectory = assetDirectory;
			ReadYAML(node, "StartScene", config.StartScene);
			ReadYAML(node, "WindowWidth", config.WindowWidth);
			ReadYAML(node, "WindowHeight", config.WindowHeight);
			ReadYAML(node, "Fullscreen", config.Fullscreen);
			ReadYAML(node, "VSync", config.VSync);
			ReadYAML(node, "RenderQuality", config.RenderQuality);
			if (!FileSystem::IsSafeRelativePath(config.AssetDirectory))
			{
				SetError(outError, "AssetDirectory must be a relative path inside the project");
				return nullptr;
			}
		}
		catch (const YAML::Exception& e)
		{
			SetError(outError, std::string("malformed project file: ") + e.what());
			return nullptr;
		}

		std::filesystem::create_directories(project->GetAssetDirectory(), ec);
		return project;
	}

	bool Project::Save() const
	{
		YAML::Node node;
		node["Name"] = m_Config.Name;
		node["AssetDirectory"] = m_Config.AssetDirectory.generic_string();
		node["StartScene"] = m_Config.StartScene;
		node["WindowWidth"] = m_Config.WindowWidth;
		node["WindowHeight"] = m_Config.WindowHeight;
		node["Fullscreen"] = m_Config.Fullscreen;
		node["VSync"] = m_Config.VSync;
		node["RenderQuality"] = m_Config.RenderQuality;
		YAML::Node root;
		root["Project"] = node;
		YAML::Emitter out;
		out << root;
		return FileSystem::WriteText(m_ProjectFile, std::string(out.c_str()) + "\n");
	}

	std::string Project::MakeAssetRelative(const std::filesystem::path& absolute) const
	{
		std::error_code ec;
		const std::filesystem::path base = std::filesystem::weakly_canonical(GetAssetDirectory(), ec);
		const std::filesystem::path target = std::filesystem::weakly_canonical(absolute, ec);
		const std::filesystem::path relative = target.lexically_relative(base);
		if (relative.empty() || !FileSystem::IsSafeRelativePath(relative))
			return {};
		return relative.generic_string();
	}

	RenderSettings Project::GetRenderSettings(const std::string& adapterName) const
	{
		const std::string quality = Lower(m_Config.RenderQuality);
		if (quality == "low")
			return RenderSettings::LowQuality();
		if (quality == "high")
			return RenderSettings{};
		// Auto: Raspberry Pi (V3D) and software rasterizers get the low preset.
		const std::string adapter = Lower(adapterName);
		if (adapter.find("v3d") != std::string::npos || adapter.find("llvmpipe") != std::string::npos ||
			adapter.find("videocore") != std::string::npos)
			return RenderSettings::LowQuality();
		return RenderSettings{};
	}

}
