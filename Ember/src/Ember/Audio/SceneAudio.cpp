#include "Ember/Audio/SceneAudio.h"

#include "Ember/Core/Log.h"
#include "Ember/Math/Math.h"
#include "Ember/Scene/Components.h"
#include "Ember/Scene/Entity.h"
#include "Ember/Scene/Scene.h"

namespace Ember {

	SceneAudio::SceneAudio(Scene* scene)
		: m_Scene(scene)
	{
	}

	SceneAudio::~SceneAudio()
	{
		OnRuntimeStop();
	}

	void SceneAudio::OnRuntimeStart()
	{
		AudioEngine::Init();
		UpdateListener();
		for (Entity entity : m_Scene->GetAllEntitiesOrdered())
		{
			if (const auto* source = entity.TryGetComponent<AudioSourceComponent>(); source && source->PlayOnStart)
				Play(entity);
		}
	}

	void SceneAudio::OnRuntimeStop()
	{
		m_OneShots.clear();
		m_Sources.clear();
	}

	SceneAudio::SourceState* SceneAudio::PrepareSource(Entity entity)
	{
		const auto* source = entity ? entity.TryGetComponent<AudioSourceComponent>() : nullptr;
		if (!source || source->Clip.empty())
			return nullptr;

		SourceState& state = m_Sources[entity.GetUUID()];
		if (!state.Voice.IsLoaded() || state.LoadedClip != source->Clip || state.LoadedSpatial != source->Spatial)
		{
			const std::filesystem::path path = m_Scene->GetAssetDirectory() / source->Clip;
			if (!state.Voice.Load(path, source->Spatial))
			{
				m_Sources.erase(entity.GetUUID());
				return nullptr;
			}
			state.LoadedClip = source->Clip;
			state.LoadedSpatial = source->Spatial;
		}

		state.Voice.SetVolume(source->Volume);
		state.Voice.SetPitch(source->Pitch);
		state.Voice.SetLooping(source->Loop);
		state.Voice.SetDistanceRange(source->MinDistance, source->MaxDistance);
		state.Voice.SetPosition(glm::vec3(m_Scene->GetWorldTransform(entity)[3]));
		return &state;
	}

	bool SceneAudio::Play(Entity entity)
	{
		SourceState* state = PrepareSource(entity);
		if (!state)
			return false;
		state->Voice.Play();
		return true;
	}

	void SceneAudio::Stop(Entity entity)
	{
		if (!entity)
			return;
		auto it = m_Sources.find(entity.GetUUID());
		if (it != m_Sources.end())
			it->second.Voice.Stop();
	}

	bool SceneAudio::IsPlaying(Entity entity)
	{
		if (!entity)
			return false;
		auto it = m_Sources.find(entity.GetUUID());
		return it != m_Sources.end() && it->second.Voice.IsPlaying();
	}

	bool SceneAudio::PlayOneShot(const std::string& clipPath, float volume, std::optional<glm::vec3> position)
	{
		Sound sound;
		if (!sound.Load(m_Scene->GetAssetDirectory() / clipPath, position.has_value()))
			return false;
		sound.SetVolume(volume);
		if (position)
			sound.SetPosition(*position);
		sound.Play();
		m_OneShots.push_back(std::move(sound));
		return true;
	}

	void SceneAudio::OnEntityDestroyed(Entity entity)
	{
		if (entity)
			m_Sources.erase(entity.GetUUID());
	}

	void SceneAudio::OnUpdate(Timestep /*ts*/)
	{
		UpdateListener();

		// Keep spatial sources attached to their entities; drop sources whose component disappeared.
		for (auto it = m_Sources.begin(); it != m_Sources.end();)
		{
			Entity entity = m_Scene->GetEntityByUUID(it->first);
			if (!entity || !entity.HasComponent<AudioSourceComponent>())
			{
				it = m_Sources.erase(it);
				continue;
			}
			it->second.Voice.SetPosition(glm::vec3(m_Scene->GetWorldTransform(entity)[3]));
			++it;
		}

		// Release finished one-shots. In device-less mode nothing advances playback, so one-shots are
		// released immediately after starting to avoid unbounded growth.
		const bool hasDevice = AudioEngine::HasDevice();
		m_OneShots.remove_if([hasDevice](const Sound& sound) { return !hasDevice || sound.IsAtEnd() || !sound.IsPlaying(); });
	}

	void SceneAudio::UpdateListener()
	{
		Entity listener;
		uint64_t bestIndex = UINT64_MAX;
		auto view = m_Scene->GetAllEntitiesWith<AudioListenerComponent, IDComponent>();
		for (entt::entity handle : view)
		{
			if (!view.get<AudioListenerComponent>(handle).Active)
				continue;
			const uint64_t index = view.get<IDComponent>(handle).CreationIndex;
			if (index < bestIndex)
			{
				bestIndex = index;
				listener = Entity(handle, m_Scene);
			}
		}
		if (!listener)
			listener = m_Scene->GetPrimaryCameraEntity();
		if (!listener)
			return;

		const glm::mat4 world = m_Scene->GetWorldTransform(listener);
		const glm::vec3 position(world[3]);
		const glm::vec3 forward = -glm::normalize(glm::vec3(world[2]));
		const glm::vec3 up = glm::normalize(glm::vec3(world[1]));
		AudioEngine::SetListener(position, forward, up);
	}

}
