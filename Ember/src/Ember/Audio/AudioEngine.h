#pragma once

#include "Ember/Core/Base.h"

#include <glm/vec3.hpp>

#include <filesystem>
#include <string>

struct ma_engine;

namespace Ember {

	// Process-wide audio engine (miniaudio). Lazily initialized on first use. If no audio device is available
	// (headless servers, CI) it falls back to a device-less engine: sounds still load, play and report state,
	// they are just not audible. This keeps game logic identical everywhere.
	class AudioEngine
	{
	public:
		static bool Init();
		static void Shutdown();
		static bool IsInitialized();
		// True if a real playback device is open (false in device-less fallback mode).
		static bool HasDevice();

		static void SetMasterVolume(float volume);
		static float GetMasterVolume();

		static void SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);

		static ma_engine* GetNativeEngine();
	};

	// A playable sound instance (one voice). Move-only.
	class Sound
	{
	public:
		Sound();
		~Sound();

		Sound(const Sound&) = delete;
		Sound& operator=(const Sound&) = delete;
		Sound(Sound&& other) noexcept;
		Sound& operator=(Sound&& other) noexcept;

		// Loads a sound file (wav, mp3, flac; decoded in memory). Returns false on failure.
		bool Load(const std::filesystem::path& path, bool spatial);
		void Unload();
		bool IsLoaded() const { return m_Sound != nullptr; }

		void Play();
		void Stop();
		bool IsPlaying() const;
		bool IsAtEnd() const;

		void SetVolume(float volume);
		void SetPitch(float pitch);
		void SetLooping(bool loop);
		void SetPosition(const glm::vec3& position);
		void SetDistanceRange(float minDistance, float maxDistance);

	private:
		void* m_Sound = nullptr; // ma_sound*
	};

}
