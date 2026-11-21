#include "Ember/Audio/AudioEngine.h"

#include "Ember/Core/Log.h"

#include <miniaudio.h>

#include <algorithm>
#include <cstdlib>
#include <mutex>

namespace Ember {

	namespace {

		std::mutex s_AudioMutex;
		ma_engine* s_Engine = nullptr;
		bool s_HasDevice = false;
		float s_MasterVolume = 1.0f;

	}

	bool AudioEngine::Init()
	{
		std::scoped_lock lock(s_AudioMutex);
		if (s_Engine)
			return true;

		s_Engine = new ma_engine();

		// EMBER_AUDIO=null forces the device-less mode (deterministic tests, servers).
		const char* mode = std::getenv("EMBER_AUDIO");
		const bool forceNull = mode && std::string(mode) == "null";

		ma_result result = MA_ERROR;
		if (!forceNull)
		{
			ma_engine_config config = ma_engine_config_init();
			result = ma_engine_init(&config, s_Engine);
		}

		if (result == MA_SUCCESS)
		{
			s_HasDevice = true;
			EMBER_CORE_INFO("Audio initialized ({} Hz)", ma_engine_get_sample_rate(s_Engine));
		}
		else
		{
			ma_engine_config config = ma_engine_config_init();
			config.noDevice = MA_TRUE;
			config.channels = 2;
			config.sampleRate = 48000;
			result = ma_engine_init(&config, s_Engine);
			if (result != MA_SUCCESS)
			{
				EMBER_CORE_ERROR("Audio engine initialization failed ({})", static_cast<int>(result));
				delete s_Engine;
				s_Engine = nullptr;
				return false;
			}
			s_HasDevice = false;
			if (!forceNull)
				EMBER_CORE_WARN("No audio device available - audio runs silently");
		}

		ma_engine_set_volume(s_Engine, s_MasterVolume);
		return true;
	}

	void AudioEngine::Shutdown()
	{
		std::scoped_lock lock(s_AudioMutex);
		if (!s_Engine)
			return;
		ma_engine_uninit(s_Engine);
		delete s_Engine;
		s_Engine = nullptr;
		s_HasDevice = false;
	}

	bool AudioEngine::IsInitialized()
	{
		return s_Engine != nullptr;
	}

	bool AudioEngine::HasDevice()
	{
		return s_HasDevice;
	}

	void AudioEngine::SetMasterVolume(float volume)
	{
		s_MasterVolume = std::max(volume, 0.0f);
		if (s_Engine)
			ma_engine_set_volume(s_Engine, s_MasterVolume);
	}

	float AudioEngine::GetMasterVolume()
	{
		return s_MasterVolume;
	}

	void AudioEngine::SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up)
	{
		if (!s_Engine)
			return;
		ma_engine_listener_set_position(s_Engine, 0, position.x, position.y, position.z);
		ma_engine_listener_set_direction(s_Engine, 0, forward.x, forward.y, forward.z);
		ma_engine_listener_set_world_up(s_Engine, 0, up.x, up.y, up.z);
	}

	ma_engine* AudioEngine::GetNativeEngine()
	{
		return s_Engine;
	}

	// ----------------------------------------------------------------------------------------------

	Sound::Sound() = default;

	Sound::~Sound()
	{
		Unload();
	}

	Sound::Sound(Sound&& other) noexcept
		: m_Sound(other.m_Sound)
	{
		other.m_Sound = nullptr;
	}

	Sound& Sound::operator=(Sound&& other) noexcept
	{
		if (this != &other)
		{
			Unload();
			m_Sound = other.m_Sound;
			other.m_Sound = nullptr;
		}
		return *this;
	}

	bool Sound::Load(const std::filesystem::path& path, bool spatial)
	{
		Unload();
		if (!AudioEngine::Init())
			return false;

		auto* sound = new ma_sound();
		ma_uint32 flags = MA_SOUND_FLAG_DECODE;
		if (!spatial)
			flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
		const ma_result result = ma_sound_init_from_file(AudioEngine::GetNativeEngine(), path.string().c_str(), flags,
			nullptr, nullptr, sound);
		if (result != MA_SUCCESS)
		{
			EMBER_CORE_ERROR("Failed to load sound '{}' ({})", path.string(), static_cast<int>(result));
			delete sound;
			return false;
		}
		m_Sound = sound;
		return true;
	}

	void Sound::Unload()
	{
		if (!m_Sound)
			return;
		auto* sound = static_cast<ma_sound*>(m_Sound);
		ma_sound_uninit(sound);
		delete sound;
		m_Sound = nullptr;
	}

	void Sound::Play()
	{
		if (!m_Sound)
			return;
		auto* sound = static_cast<ma_sound*>(m_Sound);
		if (ma_sound_at_end(sound))
			ma_sound_seek_to_pcm_frame(sound, 0);
		ma_sound_start(sound);
	}

	void Sound::Stop()
	{
		if (!m_Sound)
			return;
		auto* sound = static_cast<ma_sound*>(m_Sound);
		ma_sound_stop(sound);
		ma_sound_seek_to_pcm_frame(sound, 0);
	}

	bool Sound::IsPlaying() const
	{
		return m_Sound && ma_sound_is_playing(static_cast<ma_sound*>(m_Sound));
	}

	bool Sound::IsAtEnd() const
	{
		return m_Sound && ma_sound_at_end(static_cast<ma_sound*>(m_Sound));
	}

	void Sound::SetVolume(float volume)
	{
		if (m_Sound)
			ma_sound_set_volume(static_cast<ma_sound*>(m_Sound), std::max(volume, 0.0f));
	}

	void Sound::SetPitch(float pitch)
	{
		if (m_Sound)
			ma_sound_set_pitch(static_cast<ma_sound*>(m_Sound), std::max(pitch, 0.01f));
	}

	void Sound::SetLooping(bool loop)
	{
		if (m_Sound)
			ma_sound_set_looping(static_cast<ma_sound*>(m_Sound), loop ? MA_TRUE : MA_FALSE);
	}

	void Sound::SetPosition(const glm::vec3& position)
	{
		if (m_Sound)
			ma_sound_set_position(static_cast<ma_sound*>(m_Sound), position.x, position.y, position.z);
	}

	void Sound::SetDistanceRange(float minDistance, float maxDistance)
	{
		if (!m_Sound)
			return;
		auto* sound = static_cast<ma_sound*>(m_Sound);
		ma_sound_set_min_distance(sound, std::max(minDistance, 0.0f));
		ma_sound_set_max_distance(sound, std::max(maxDistance, minDistance));
	}

}
