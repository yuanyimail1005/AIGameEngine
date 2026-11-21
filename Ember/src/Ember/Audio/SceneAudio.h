#pragma once

#include "Ember/Audio/AudioEngine.h"
#include "Ember/Core/Timestep.h"
#include "Ember/Core/UUID.h"

#include <glm/vec3.hpp>

#include <list>
#include <optional>
#include <string>
#include <unordered_map>

namespace Ember {

	class Entity;
	class Scene;

	// Audio playback for a running scene: AudioSource components, the listener, and fire-and-forget one-shots.
	class SceneAudio
	{
	public:
		explicit SceneAudio(Scene* scene);
		~SceneAudio();

		SceneAudio(const SceneAudio&) = delete;
		SceneAudio& operator=(const SceneAudio&) = delete;

		void OnRuntimeStart();
		void OnRuntimeStop();
		void OnUpdate(Timestep ts);
		void OnEntityDestroyed(Entity entity);

		// AudioSource control (loads the clip on first use). Return false if the entity has no usable source.
		bool Play(Entity entity);
		void Stop(Entity entity);
		bool IsPlaying(Entity entity);

		// Plays a clip once at an optional world position (non-spatial if no position). Returns false if the
		// clip could not be loaded.
		bool PlayOneShot(const std::string& clipPath, float volume = 1.0f, std::optional<glm::vec3> position = std::nullopt);
		size_t GetActiveOneShotCount() const { return m_OneShots.size(); }

	private:
		struct SourceState
		{
			Sound Voice;
			std::string LoadedClip;
			bool LoadedSpatial = false;
		};

		SourceState* PrepareSource(Entity entity);
		void UpdateListener();

	private:
		Scene* m_Scene;
		std::unordered_map<UUID, SourceState> m_Sources;
		std::list<Sound> m_OneShots;
	};

}
