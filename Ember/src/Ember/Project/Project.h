#pragma once

#include "Ember/Core/Base.h"
#include "Ember/Renderer/RenderTypes.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Ember {

	struct ProjectConfig
	{
		std::string Name = "Untitled";
		std::filesystem::path AssetDirectory = "Assets"; // relative to the project directory
		std::string StartScene;                          // asset-relative path of the scene the game starts with
		uint32_t WindowWidth = 1280;
		uint32_t WindowHeight = 720;
		bool Fullscreen = false;
		bool VSync = true;
		// "Auto" picks Low on Raspberry Pi class GPUs and High elsewhere.
		std::string RenderQuality = "Auto";
	};

	// A game project: a directory containing <Name>.eproject and an asset directory.
	class Project
	{
	public:
		static constexpr const char* c_Extension = ".eproject";

		// Creates a new project directory structure (Assets/{Scenes,Scripts,Prefabs,Materials,Models,Textures,Audio,HDRI})
		// with a project file and an empty start scene. Fails if a project file already exists there.
		static Ref<Project> Create(const std::filesystem::path& directory, const std::string& name, std::string* outError = nullptr);
		// Loads a project from a .eproject file or from a directory containing exactly one.
		static Ref<Project> Load(const std::filesystem::path& path, std::string* outError = nullptr);

		bool Save() const;

		ProjectConfig& GetConfig() { return m_Config; }
		const ProjectConfig& GetConfig() const { return m_Config; }
		const std::filesystem::path& GetProjectDirectory() const { return m_ProjectDirectory; }
		const std::filesystem::path& GetProjectFile() const { return m_ProjectFile; }
		std::filesystem::path GetAssetDirectory() const { return m_ProjectDirectory / m_Config.AssetDirectory; }
		// Absolute path of an asset-relative path.
		std::filesystem::path GetAssetPath(const std::filesystem::path& relative) const { return GetAssetDirectory() / relative; }
		// Asset-relative path of an absolute path inside the asset directory (empty if outside).
		std::string MakeAssetRelative(const std::filesystem::path& absolute) const;

		RenderSettings GetRenderSettings(const std::string& adapterName) const;

	private:
		ProjectConfig m_Config;
		std::filesystem::path m_ProjectDirectory;
		std::filesystem::path m_ProjectFile;
	};

}
