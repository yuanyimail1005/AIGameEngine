#include "GameExporter.h"

#include "Ember/Core/FileSystem.h"
#include "Ember/Core/Log.h"
#include "Ember/Project/Project.h"

#include <system_error>

namespace Ember {

	ExportResult ExportGame(const Project& project, const std::filesystem::path& outputDirectory,
		const std::filesystem::path& runtimeExecutable)
	{
		ExportResult result;
		std::error_code ec;

		if (!std::filesystem::is_regular_file(runtimeExecutable, ec))
		{
			result.Error = "runtime executable not found at '" + runtimeExecutable.string() + "' (build the EmberRuntime target)";
			return result;
		}
		const ProjectConfig& config = project.GetConfig();
		if (config.StartScene.empty() || !FileSystem::Exists(project.GetAssetPath(config.StartScene)))
		{
			result.Error = "the project's start scene '" + config.StartScene + "' does not exist";
			return result;
		}

		// Refuse to export into the project itself (would recursively copy assets).
		const std::filesystem::path output = std::filesystem::weakly_canonical(outputDirectory, ec);
		const std::filesystem::path projectDir = std::filesystem::weakly_canonical(project.GetProjectDirectory(), ec);
		const std::string relative = output.lexically_relative(projectDir).generic_string();
		if (output == projectDir || (!relative.empty() && relative.rfind("..", 0) != 0))
		{
			result.Error = "the export directory must be outside the project directory";
			return result;
		}

		std::filesystem::create_directories(output, ec);
		if (ec)
		{
			result.Error = "cannot create '" + output.string() + "': " + ec.message();
			return result;
		}

		// Executable named after the game.
		std::string executableName = config.Name;
		for (char& c : executableName)
		{
			if (c == ' ' || c == '/' || c == '\\' || c == ':')
				c = '_';
		}
		executableName += runtimeExecutable.extension().string();
		result.Executable = output / executableName;
		std::filesystem::copy_file(runtimeExecutable, result.Executable, std::filesystem::copy_options::overwrite_existing, ec);
		if (ec)
		{
			result.Error = "failed to copy the runtime: " + ec.message();
			return result;
		}
		std::filesystem::permissions(result.Executable,
			std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
			std::filesystem::perm_options::add, ec);
		result.FilesCopied++;

		// Project file
		std::filesystem::copy_file(project.GetProjectFile(), output / project.GetProjectFile().filename(),
			std::filesystem::copy_options::overwrite_existing, ec);
		if (ec)
		{
			result.Error = "failed to copy the project file: " + ec.message();
			return result;
		}
		result.FilesCopied++;

		// Assets (replace any previous export's assets so deleted files do not linger)
		const std::filesystem::path assetsOut = output / config.AssetDirectory;
		std::filesystem::remove_all(assetsOut, ec);
		for (const auto& entry : std::filesystem::recursive_directory_iterator(project.GetAssetDirectory(), ec))
		{
			const std::filesystem::path relativePath = entry.path().lexically_relative(project.GetAssetDirectory());
			const std::filesystem::path target = assetsOut / relativePath;
			if (entry.is_directory())
			{
				std::filesystem::create_directories(target, ec);
				continue;
			}
			std::filesystem::create_directories(target.parent_path(), ec);
			std::filesystem::copy_file(entry.path(), target, std::filesystem::copy_options::overwrite_existing, ec);
			if (ec)
			{
				result.Error = "failed to copy '" + entry.path().string() + "': " + ec.message();
				return result;
			}
			result.FilesCopied++;
		}

		result.Success = true;
		EMBER_CORE_INFO("Exported '{}' to {} ({} files)", config.Name, output.string(), result.FilesCopied);
		return result;
	}

}
