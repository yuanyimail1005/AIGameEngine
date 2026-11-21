#include "TestUtils.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

namespace EmberTest {

	std::filesystem::path MakeTempDirectory(const std::string& name)
	{
		const std::filesystem::path path = std::filesystem::temp_directory_path() / "ember_tests" / name;
		std::filesystem::remove_all(path);
		std::filesystem::create_directories(path);
		return path;
	}

	void WriteFile(const std::filesystem::path& path, const std::string& content)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		stream << content;
	}

	namespace {

		template<typename T>
		void WriteLE(std::ofstream& stream, T value)
		{
			for (size_t i = 0; i < sizeof(T); i++)
				stream.put(static_cast<char>((static_cast<uint64_t>(value) >> (8 * i)) & 0xFF));
		}

	}

	void WriteSineWav(const std::filesystem::path& path, float seconds, float frequency, uint32_t sampleRate)
	{
		std::filesystem::create_directories(path.parent_path());
		const uint32_t frames = static_cast<uint32_t>(seconds * static_cast<float>(sampleRate));
		const uint32_t dataBytes = frames * 2;

		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		stream.write("RIFF", 4);
		WriteLE<uint32_t>(stream, 36 + dataBytes);
		stream.write("WAVE", 4);
		stream.write("fmt ", 4);
		WriteLE<uint32_t>(stream, 16);
		WriteLE<uint16_t>(stream, 1);  // PCM
		WriteLE<uint16_t>(stream, 1);  // mono
		WriteLE<uint32_t>(stream, sampleRate);
		WriteLE<uint32_t>(stream, sampleRate * 2);
		WriteLE<uint16_t>(stream, 2);
		WriteLE<uint16_t>(stream, 16);
		stream.write("data", 4);
		WriteLE<uint32_t>(stream, dataBytes);
		for (uint32_t i = 0; i < frames; i++)
		{
			const float sample = std::sin(2.0f * 3.14159265f * frequency * static_cast<float>(i) / static_cast<float>(sampleRate));
			WriteLE<uint16_t>(stream, static_cast<uint16_t>(static_cast<int16_t>(sample * 12000.0f)));
		}
	}

}
