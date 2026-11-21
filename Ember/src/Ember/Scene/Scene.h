#pragma once

#include "Ember/Core/Base.h"
#include "Ember/Core/Timestep.h"
#include "Ember/Core/UUID.h"

#include <entt/entt.hpp>
#include <glm/mat4x4.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Ember {

	class Entity;
	class PhysicsWorld;
	class ScriptEngine;
	class SceneAudio;

	enum class SceneState : uint8_t
	{
		Edit = 0, Play
	};

	class Scene
	{
	public:
		explicit Scene(std::string name = "Untitled");
		~Scene();

		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;

		// Deep copy of all entities and components (used to enter play mode without touching the edited scene).
		static Ref<Scene> Copy(const Ref<Scene>& other);

		// ------------------------------------------------------------------------------------------
		// Entities
		Entity CreateEntity(const std::string& name = std::string());
		Entity CreateEntityWithUUID(UUID uuid, const std::string& name = std::string());
		// Destroys the entity and all its children. While the scene is running, destruction is deferred to the
		// end of the frame so scripts and systems never see dangling entities mid-update.
		void DestroyEntity(Entity entity);
		// Deep copy (including children) with fresh UUIDs, placed next to the original (same parent).
		Entity DuplicateEntity(Entity entity);

		Entity FindEntityByName(std::string_view name);
		Entity GetEntityByUUID(UUID uuid);
		bool HasEntity(UUID uuid) const;
		Entity GetPrimaryCameraEntity();
		size_t GetEntityCount() const { return m_EntityMap.size(); }
		// All entities sorted by creation order.
		std::vector<Entity> GetAllEntitiesOrdered();
		// Entities without a parent, sorted by creation order.
		std::vector<Entity> GetRootEntities();

		template<typename... Components>
		auto GetAllEntitiesWith() { return m_Registry.view<Components...>(); }

		// ------------------------------------------------------------------------------------------
		// Hierarchy
		// Re-parents child under parent (invalid parent = make root). Returns false if this would create a cycle.
		bool SetParent(Entity child, Entity parent, bool keepWorldTransform = true);
		Entity GetParent(Entity entity);
		std::vector<Entity> GetChildren(Entity entity);
		bool IsDescendantOf(Entity entity, Entity potentialAncestor);
		glm::mat4 GetWorldTransform(Entity entity);
		// Sets the entity's local transform so that its world transform equals the given matrix.
		void SetWorldTransform(Entity entity, const glm::mat4& worldTransform);

		// ------------------------------------------------------------------------------------------
		// Simulation
		void OnRuntimeStart();
		void OnRuntimeStop();
		void OnUpdateRuntime(Timestep ts);
		void OnUpdateEditor(Timestep ts);
		void OnViewportResize(uint32_t width, uint32_t height);

		bool IsRunning() const { return m_State == SceneState::Play; }
		bool IsPaused() const { return m_Paused; }
		void SetPaused(bool paused) { m_Paused = paused; }
		// While paused, advance the simulation by the given number of frames.
		void Step(int frames = 1) { m_StepFrames = frames; }
		// Total simulated time since OnRuntimeStart (seconds).
		double GetRuntimeTime() const { return m_RuntimeTime; }

		uint32_t GetViewportWidth() const { return m_ViewportWidth; }
		uint32_t GetViewportHeight() const { return m_ViewportHeight; }

		PhysicsWorld* GetPhysicsWorld() const { return m_PhysicsWorld.get(); }
		ScriptEngine* GetScriptEngine() const { return m_ScriptEngine.get(); }
		SceneAudio* GetAudio() const { return m_Audio.get(); }

		// Project asset directory used to resolve asset paths for runtime systems (scripts, audio, prefabs).
		void SetAssetDirectory(const std::filesystem::path& directory) { m_AssetDirectory = directory; }
		const std::filesystem::path& GetAssetDirectory() const { return m_AssetDirectory; }

		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& name) { m_Name = name; }

		entt::registry& GetRegistry() { return m_Registry; }

		// Flushes deferred entity destruction (called automatically at the end of OnUpdateRuntime).
		void FlushPendingDestruction();

	private:
		void DestroyEntityImmediate(Entity entity);
		Entity DuplicateEntityRecursive(Entity source, Entity newParent);
		void UpdateCameraAspectRatios();

		template<typename T>
		void OnComponentAdded(Entity entity, T& component);

	private:
		std::string m_Name;
		entt::registry m_Registry;
		std::unordered_map<UUID, entt::entity> m_EntityMap;
		uint64_t m_NextCreationIndex = 1;

		SceneState m_State = SceneState::Edit;
		bool m_Paused = false;
		int m_StepFrames = 0;
		double m_RuntimeTime = 0.0;
		uint32_t m_ViewportWidth = 0;
		uint32_t m_ViewportHeight = 0;
		std::vector<UUID> m_PendingDestroy;
		std::filesystem::path m_AssetDirectory;

		Scope<PhysicsWorld> m_PhysicsWorld;
		Scope<ScriptEngine> m_ScriptEngine;
		Scope<SceneAudio> m_Audio;

		friend class Entity;
		friend class SceneSerializer;
	};

}
