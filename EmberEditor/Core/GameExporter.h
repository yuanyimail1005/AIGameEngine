#pragma once

#include <filesystem>
#include <string>

namespace Ember {

	class Project;

	struct ExportResult
	{
		bool Success = false;
		std::string Error;
		std::filesystem::path Executable; // path of the exported game executable
		size_t FilesCopied = 0;
	};

	// Packages a project into a standalone, distributable directory:
	//   <output>/<GameName>[.exe]     the Ember runtime (no editor tooling)
	//   <output>/<GameName>.eproject  project settings
	//   <output>/<AssetDirectory>/    all assets
	// The runtime finds the project file next to itself, so the folder can be zipped and shipped as is.
	ExportResult ExportGame(const Project& project, const std::filesystem::path& outputDirectory,
		const std::filesystem::path& runtimeExecutable);

}
