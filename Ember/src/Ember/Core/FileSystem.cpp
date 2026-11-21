#include "Ember/Core/FileSystem.h"

#include "Ember/Core/Base.h"
#include "Ember/Core/Log.h"

#include <fstream>
#include <system_error>

#if defined(EMBER_PLATFORM_WINDOWS)
	#define WIN32_LEAN_AND_MEAN
	#define NOMINMAX
	#include <windows.h>
#elif defined(EMBER_PLATFORM_MACOS)
	#include <mach-o/dyld.h>
	#include <climits>
#elif defined(EMBER_PLATFORM_LINUX)
	#include <unistd.h>
	#include <climits>
#endif

namespace Ember {

	std::optional<Buffer> FileSystem::ReadBinary(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary | std::ios::ate);
		if (!stream)
			return std::nullopt;

		const std::streamoff end = stream.tellg();
		if (end < 0)
			return std::nullopt;
		stream.seekg(0, std::ios::beg);

		Buffer buffer(static_cast<size_t>(end));
		if (end > 0 && !stream.read(reinterpret_cast<char*>(buffer.Data()), end))
			return std::nullopt;
		return buffer;
	}

	std::optional<std::string> FileSystem::ReadText(const std::filesystem::path& path)
	{
		std::optional<Buffer> buffer = ReadBinary(path);
		if (!buffer)
			return std::nullopt;
		return std::string(reinterpret_cast<const char*>(buffer->Data()), buffer->Size());
	}

	bool FileSystem::WriteBinary(const std::filesystem::path& path, const void* data, size_t size)
	{
		std::error_code ec;
		if (path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), ec);

		std::filesystem::path tempPath = path;
		tempPath += ".tmp";
		{
			std::ofstream stream(tempPath, std::ios::binary | std::ios::trunc);
			if (!stream)
			{
				EMBER_CORE_ERROR("FileSystem: cannot open '{}' for writing", tempPath.string());
				return false;
			}
			if (size > 0)
				stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
			if (!stream.good())
			{
				EMBER_CORE_ERROR("FileSystem: failed writing '{}'", tempPath.string());
				return false;
			}
		}

		std::filesystem::rename(tempPath, path, ec);
		if (ec)
		{
			EMBER_CORE_ERROR("FileSystem: failed to move '{}' into place: {}", path.string(), ec.message());
			std::filesystem::remove(tempPath, ec);
			return false;
		}
		return true;
	}

	bool FileSystem::WriteText(const std::filesystem::path& path, std::string_view text)
	{
		return WriteBinary(path, text.data(), text.size());
	}

	bool FileSystem::Exists(const std::filesystem::path& path)
	{
		std::error_code ec;
		return std::filesystem::exists(path, ec);
	}

	bool FileSystem::CreateDirectories(const std::filesystem::path& path)
	{
		std::error_code ec;
		std::filesystem::create_directories(path, ec);
		return !ec && std::filesystem::is_directory(path, ec);
	}

	std::filesystem::path FileSystem::GetExecutableDirectory()
	{
#if defined(EMBER_PLATFORM_WINDOWS)
		wchar_t buffer[MAX_PATH];
		const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		return std::filesystem::path(std::wstring(buffer, length)).parent_path();
#elif defined(EMBER_PLATFORM_MACOS)
		char buffer[PATH_MAX];
		uint32_t size = sizeof(buffer);
		if (_NSGetExecutablePath(buffer, &size) == 0)
			return std::filesystem::weakly_canonical(buffer).parent_path();
		return std::filesystem::current_path();
#else
		char buffer[PATH_MAX];
		const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
		if (length > 0)
			return std::filesystem::path(std::string(buffer, static_cast<size_t>(length))).parent_path();
		return std::filesystem::current_path();
#endif
	}

}
