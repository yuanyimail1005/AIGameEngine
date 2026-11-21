#include "Ember/Scene/Scene.h"

#include "Ember/Audio/SceneAudio.h"
#include "Ember/Core/Log.h"
#include "Ember/Math/Math.h"
#include "Ember/Physics/PhysicsWorld.h"
#include "Ember/Scene/Components.h"
#include "Ember/Scene/Entity.h"
#include "Ember/Scripting/ScriptEngine.h"

#include <algorithm>

namespace Ember {

	namespace {

		template<typename... Component>
		void CopyComponentIfExists(ComponentGroup<Component...>, Entity destination, Entity source)
		{
			([&]()
			{
				if (const Component* component = source.TryGetComponent<Component>())
					destination.AddOrReplaceComponent<Component>(*component);
			}(), ...);
		}

		template<typename... Component>
		void CopyAllComponents(ComponentGroup<Component...>, entt::registry& dst, entt::registry& src,
			const std::unordered_map<UUID, entt::entity>& dstEntityMap)
		{
			([&]()
			{
				auto view = src.view<Component>();
				for (entt::entity srcEntity : view)
				{
					const UUID uuid = src.get<IDComponent>(srcEntity).ID;
					auto it = dstEntityMap.find(uuid);
					if (it != dstEntityMap.end())
						dst.emplace_or_replace<Component>(it->second, view.template get<Component>(srcEntity));
				}
			}(), ...);
		}

	}

	Scene::Scene(std::string name)
		: m_Name(std::move(name))
	{
	}

	Scene::~Scene()
	{
		if (m_State == SceneState::Play)
			OnRuntimeStop();
		m_Registry.clear();
	}

	Ref<Scene> Scene::Copy(const Ref<Scene>& other)
	{
		Ref<Scene> scene = CreateRef<Scene>(other->m_Name);
		scene->m_ViewportWidth = other->m_ViewportWidth;
		scene->m_ViewportHeight = other->m_ViewportHeight;
		scene->m_AssetDirectory = other->m_AssetDirectory;

		// Recreate entities in creation order so the copy orders identically.
		for (Entity source : other->GetAllEntitiesOrdered())
		{
			Entity copy = scene->CreateEntityWithUUID(source.GetUUID(), source.GetName());
			copy.GetComponent<IDComponent>().CreationIndex = source.GetComponent<IDComponent>().CreationIndex;
		}
		scene->m_NextCreationIndex = other->m_NextCreationIndex;

		CopyAllComponents(AllComponents{}, scene->m_Registry, other->m_Registry, scene->m_EntityMap);
		return scene;
	}

	Entity Scene::CreateEntity(const std::string& name)
	{
		return CreateEntityWithUUID(UUID(), name);
	}

	Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
	{
		if (!uuid.IsValid() || m_EntityMap.contains(uuid))
		{
			EMBER_CORE_WARN("CreateEntityWithUUID: UUID {} is invalid or already in use; generating a new one",
				static_cast<uint64_t>(uuid));
			uuid = UUID();
		}

		Entity entity(m_Registry.create(), this);
		entity.AddComponent<IDComponent>(IDComponent{ uuid, m_NextCreationIndex++ });
		entity.AddComponent<TagComponent>(TagComponent{ name.empty() ? std::string("Entity") : name });
		entity.AddComponent<TransformComponent>();
		entity.AddComponent<RelationshipComponent>();
		m_EntityMap[uuid] = entity;
		return entity;
	}

	void Scene::DestroyEntity(Entity entity)
	{
		if (!entity || entity.GetScene() != this)
			return;

		if (m_State == SceneState::Play)
		{
			const UUID uuid = entity.GetUUID();
			if (std::find(m_PendingDestroy.begin(), m_PendingDestroy.end(), uuid) == m_PendingDestroy.end())
				m_PendingDestroy.push_back(uuid);
			return;
		}
		DestroyEntityImmediate(entity);
	}

	void Scene::DestroyEntityImmediate(Entity entity)
	{
		// Children first (copy the list: destroying modifies it).
		const std::vector<UUID> children = entity.GetComponent<RelationshipComponent>().Children;
		for (UUID childID : children)
		{
			if (Entity child = GetEntityByUUID(childID))
				DestroyEntityImmediate(child);
		}

		if (m_State == SceneState::Play)
		{
			if (m_ScriptEngine)
				m_ScriptEngine->OnEntityDestroyed(entity);
			if (m_PhysicsWorld)
				m_PhysicsWorld->DestroyBody(entity);
			if (m_Audio)
				m_Audio->OnEntityDestroyed(entity);
		}

		// Detach from parent.
		const UUID uuid = entity.GetUUID();
		const UUID parentID = entity.GetComponent<RelationshipComponent>().Parent;
		if (Entity parent = GetEntityByUUID(parentID))
		{
			auto& siblings = parent.GetComponent<RelationshipComponent>().Children;
			siblings.erase(std::remove(siblings.begin(), siblings.end(), uuid), siblings.end());
		}

		m_EntityMap.erase(uuid);
		m_Registry.destroy(entity);
	}

	void Scene::FlushPendingDestruction()
	{
		// Destroying can queue more (scripts' OnDestroy may destroy others): loop until stable.
		while (!m_PendingDestroy.empty())
		{
			std::vector<UUID> pending;
			pending.swap(m_PendingDestroy);
			for (UUID uuid : pending)
			{
				if (Entity entity = GetEntityByUUID(uuid))
					DestroyEntityImmediate(entity);
			}
		}
	}

	Entity Scene::DuplicateEntity(Entity entity)
	{
		if (!entity || entity.GetScene() != this)
			return {};
		Entity parent = GetParent(entity);
		Entity copy = DuplicateEntityRecursive(entity, parent);
		return copy;
	}

	Entity Scene::DuplicateEntityRecursive(Entity source, Entity newParent)
	{
		Entity copy = CreateEntity(source.GetName());
		CopyComponentIfExists(AllComponents{}, copy, source);

		// Rebuild relationships with the new UUIDs (the copied component still references the source's).
		auto& relationship = copy.GetComponent<RelationshipComponent>();
		relationship.Parent = 0;
		relationship.Children.clear();
		if (newParent)
		{
			relationship.Parent = newParent.GetUUID();
			newParent.GetComponent<RelationshipComponent>().Children.push_back(copy.GetUUID());
		}

		const std::vector<UUID> children = source.GetComponent<RelationshipComponent>().Children;
		for (UUID childID : children)
		{
			if (Entity child = GetEntityByUUID(childID))
				DuplicateEntityRecursive(child, copy);
		}
		return copy;
	}

	Entity Scene::FindEntityByName(std::string_view name)
	{
		Entity found;
		uint64_t bestIndex = UINT64_MAX;
		auto view = m_Registry.view<TagComponent, IDComponent>();
		for (entt::entity handle : view)
		{
			// Deterministic: the earliest-created entity with this name wins.
			if (view.get<TagComponent>(handle).Tag == name)
			{
				const uint64_t index = view.get<IDComponent>(handle).CreationIndex;
				if (index < bestIndex)
				{
					bestIndex = index;
					found = Entity(handle, this);
				}
			}
		}
		return found;
	}

	Entity Scene::GetEntityByUUID(UUID uuid)
	{
		auto it = m_EntityMap.find(uuid);
		if (it == m_EntityMap.end())
			return {};
		return Entity(it->second, this);
	}

	bool Scene::HasEntity(UUID uuid) const
	{
		return m_EntityMap.contains(uuid);
	}

	Entity Scene::GetPrimaryCameraEntity()
	{
		Entity found;
		uint64_t bestIndex = UINT64_MAX;
		auto view = m_Registry.view<CameraComponent, IDComponent>();
		for (entt::entity handle : view)
		{
			if (!view.get<CameraComponent>(handle).Primary)
				continue;
			const uint64_t index = view.get<IDComponent>(handle).CreationIndex;
			if (index < bestIndex)
			{
				bestIndex = index;
				found = Entity(handle, this);
			}
		}
		return found;
	}

	std::vector<Entity> Scene::GetAllEntitiesOrdered()
	{
		std::vector<std::pair<uint64_t, entt::entity>> sorted;
		sorted.reserve(m_EntityMap.size());
		auto view = m_Registry.view<IDComponent>();
		for (entt::entity handle : view)
			sorted.emplace_back(view.get<IDComponent>(handle).CreationIndex, handle);
		std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

		std::vector<Entity> result;
		result.reserve(sorted.size());
		for (const auto& [index, handle] : sorted)
			result.emplace_back(handle, this);
		return result;
	}

	std::vector<Entity> Scene::GetRootEntities()
	{
		std::vector<Entity> roots;
		for (Entity entity : GetAllEntitiesOrdered())
		{
			if (!entity.GetComponent<RelationshipComponent>().Parent.IsValid())
				roots.push_back(entity);
		}
		return roots;
	}

	bool Scene::SetParent(Entity child, Entity parent, bool keepWorldTransform)
	{
		if (!child || child.GetScene() != this)
			return false;
		if (parent && (parent == child || IsDescendantOf(parent, child)))
		{
			EMBER_CORE_WARN("SetParent: cannot parent '{}' to '{}' (would create a cycle)", child.GetName(), parent.GetName());
			return false;
		}

		const glm::mat4 world = GetWorldTransform(child);
		auto& relationship = child.GetComponent<RelationshipComponent>();
		const UUID childID = child.GetUUID();

		if (Entity oldParent = GetEntityByUUID(relationship.Parent))
		{
			auto& siblings = oldParent.GetComponent<RelationshipComponent>().Children;
			siblings.erase(std::remove(siblings.begin(), siblings.end(), childID), siblings.end());
		}

		relationship.Parent = parent ? parent.GetUUID() : UUID(0);
		if (parent)
			parent.GetComponent<RelationshipComponent>().Children.push_back(childID);

		if (keepWorldTransform)
			SetWorldTransform(child, world);
		return true;
	}

	Entity Scene::GetParent(Entity entity)
	{
		if (!entity)
			return {};
		return GetEntityByUUID(entity.GetComponent<RelationshipComponent>().Parent);
	}

	std::vector<Entity> Scene::GetChildren(Entity entity)
	{
		std::vector<Entity> children;
		if (!entity)
			return children;
		for (UUID childID : entity.GetComponent<RelationshipComponent>().Children)
		{
			if (Entity child = GetEntityByUUID(childID))
				children.push_back(child);
		}
		return children;
	}

	bool Scene::IsDescendantOf(Entity entity, Entity potentialAncestor)
	{
		Entity current = GetParent(entity);
		size_t guard = 0;
		while (current && guard++ < m_EntityMap.size())
		{
			if (current == potentialAncestor)
				return true;
			current = GetParent(current);
		}
		return false;
	}

	glm::mat4 Scene::GetWorldTransform(Entity entity)
	{
		if (!entity)
			return glm::mat4(1.0f);
		glm::mat4 transform = entity.GetComponent<TransformComponent>().GetTransform();
		Entity parent = GetParent(entity);
		size_t guard = 0;
		while (parent && guard++ < m_EntityMap.size())
		{
			transform = parent.GetComponent<TransformComponent>().GetTransform() * transform;
			parent = GetParent(parent);
		}
		return transform;
	}

	void Scene::SetWorldTransform(Entity entity, const glm::mat4& worldTransform)
	{
		if (!entity)
			return;
		glm::mat4 local = worldTransform;
		if (Entity parent = GetParent(entity))
			local = glm::inverse(GetWorldTransform(parent)) * worldTransform;

		auto& transform = entity.GetComponent<TransformComponent>();
		glm::vec3 translation, rotation, scale;
		if (Math::DecomposeTransform(local, translation, rotation, scale))
		{
			transform.Translation = translation;
			transform.Rotation = rotation;
			transform.Scale = scale;
		}
	}

	void Scene::OnRuntimeStart()
	{
		if (m_State == SceneState::Play)
			return;
		m_State = SceneState::Play;
		m_RuntimeTime = 0.0;
		m_Paused = false;
		m_StepFrames = 0;

		m_PhysicsWorld = CreateScope<PhysicsWorld>(this);
		m_PhysicsWorld->OnRuntimeStart();

		m_Audio = CreateScope<SceneAudio>(this);
		m_Audio->OnRuntimeStart();

		// Scripts last: their OnCreate may use physics and audio.
		m_ScriptEngine = CreateScope<ScriptEngine>(this);
		m_ScriptEngine->OnRuntimeStart();
	}

	void Scene::OnRuntimeStop()
	{
		if (m_State != SceneState::Play)
			return;

		if (m_ScriptEngine)
			m_ScriptEngine->OnRuntimeStop();
		FlushPendingDestruction();
		m_ScriptEngine.reset();

		if (m_Audio)
			m_Audio->OnRuntimeStop();
		m_Audio.reset();

		if (m_PhysicsWorld)
			m_PhysicsWorld->OnRuntimeStop();
		m_PhysicsWorld.reset();

		m_State = SceneState::Edit;
		m_PendingDestroy.clear();
	}

	void Scene::OnUpdateRuntime(Timestep ts)
	{
		if (m_State != SceneState::Play)
			return;

		if (m_Paused)
		{
			if (m_StepFrames <= 0)
				return;
			m_StepFrames--;
		}

		m_RuntimeTime += ts.GetSeconds();
		UpdateCameraAspectRatios();

		m_ScriptEngine->OnUpdate(ts);
		FlushPendingDestruction();

		m_PhysicsWorld->Step(ts);
		// Collision callbacks run scripts, which may destroy entities.
		m_ScriptEngine->DispatchPhysicsEvents();
		FlushPendingDestruction();

		m_ScriptEngine->OnLateUpdate(ts);
		FlushPendingDestruction();

		m_Audio->OnUpdate(ts);
	}

	void Scene::OnUpdateEditor(Timestep /*ts*/)
	{
		UpdateCameraAspectRatios();
	}

	void Scene::OnViewportResize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0 || (m_ViewportWidth == width && m_ViewportHeight == height))
			return;
		m_ViewportWidth = width;
		m_ViewportHeight = height;
		UpdateCameraAspectRatios();
	}

	void Scene::UpdateCameraAspectRatios()
	{
		if (m_ViewportWidth == 0 || m_ViewportHeight == 0)
			return;
		auto view = m_Registry.view<CameraComponent>();
		for (entt::entity handle : view)
		{
			auto& camera = view.get<CameraComponent>(handle);
			if (!camera.FixedAspectRatio)
				camera.Camera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
		}
	}

}
