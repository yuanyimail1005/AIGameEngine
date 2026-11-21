#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace EmberTest {

	// Creates a fresh, empty temporary directory unique to the calling test and returns it.
	std::filesystem::path MakeTempDirectory(const std::string& name);

	// Writes a text file (creating parent directories).
	void WriteFile(const std::filesystem::path& path, const std::string& content);

	// Writes a mono 16-bit PCM WAV with a sine tone.
	void WriteSineWav(const std::filesystem::path& path, float seconds, float frequency = 440.0f, uint32_t sampleRate = 22050);

}
