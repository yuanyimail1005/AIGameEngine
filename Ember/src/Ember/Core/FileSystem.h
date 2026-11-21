#pragma once

#include "Ember/Core/Buffer.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Ember {

	class FileSystem
	{
	public:
		static std::optional<Buffer> ReadBinary(const std::filesystem::path& path);
		static std::optional<std::string> ReadText(const std::filesystem::path& path);

		// Writes atomically (temp file + rename) so a crash never leaves a half-written file behind.
		static bool WriteBinary(const std::filesystem::path& path, const void* data, size_t size);
		static bool WriteText(const std::filesystem::path& path, std::string_view text);

		static bool Exists(const std::filesystem::path& path);
		static bool CreateDirectories(const std::filesystem::path& path);

		// Directory containing the running executable.
		static std::filesystem::path GetExecutableDirectory();
	};

}
