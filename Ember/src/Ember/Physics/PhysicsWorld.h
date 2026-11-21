#pragma once

#include "Ember/Core/Base.h"
#include "Ember/Core/Timestep.h"
#include "Ember/Core/UUID.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <optional>
#include <vector>

namespace Ember {

	class Entity;
	class Scene;

	struct RaycastHit
	{
		UUID EntityID = 0;
		glm::vec3 Point{ 0.0f };
		glm::vec3 Normal{ 0.0f };
		float Distance = 0.0f;
	};

	struct ContactEvent
	{
		UUID EntityA = 0;
		UUID EntityB = 0;
		bool Begin = true;     // false = contact ended
		bool IsTrigger = false; // at least one of the two bodies is a trigger (sensor)
	};

	// Rigid body simulation for one running scene (Jolt Physics). Created by Scene::OnRuntimeStart.
	//
	// Bodies are created lazily for every entity that has collider components (with or without a
	// RigidBodyComponent; collider-only entities are static), so entities spawned at runtime get physics
	// automatically. Changing an entity's transform from a script teleports its body; kinematic bodies follow
	// their transform smoothly. The simulation runs at a fixed 60 Hz.
	class PhysicsWorld
	{
	public:
		static constexpr float c_FixedTimestep = 1.0f / 60.0f;
		static constexpr int c_MaxSubSteps = 4;

		explicit PhysicsWorld(Scene* scene);
		~PhysicsWorld();

		PhysicsWorld(const PhysicsWorld&) = delete;
		PhysicsWorld& operator=(const PhysicsWorld&) = delete;

		void OnRuntimeStart();
		void OnRuntimeStop();
		void Step(Timestep ts);

		// Creates (or recreates) the body from the entity's current components. Call after changing collider
		// shapes or rigid body settings at runtime.
		void RecreateBody(Entity entity);
		void DestroyBody(Entity entity);
		bool HasBody(Entity entity) const;
		size_t GetBodyCount() const;

		// Body manipulation (no-ops for entities without a dynamic/kinematic body).
		void AddForce(Entity entity, const glm::vec3& force);
		void AddImpulse(Entity entity, const glm::vec3& impulse);
		void AddTorque(Entity entity, const glm::vec3& torque);
		void SetLinearVelocity(Entity entity, const glm::vec3& velocity);
		glm::vec3 GetLinearVelocity(Entity entity) const;
		void SetAngularVelocity(Entity entity, const glm::vec3& velocity);
		glm::vec3 GetAngularVelocity(Entity entity) const;

		void SetGravity(const glm::vec3& gravity);
		glm::vec3 GetGravity() const;

		// Closest hit along the ray (direction need not be normalized). Trigger bodies are ignored.
		std::optional<RaycastHit> Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const;

		// Returns and clears contact events accumulated since the last call (begin/end per entity pair).
		std::vector<ContactEvent> ConsumeContactEvents();

	private:
		struct Impl;
		Scope<Impl> m_Impl;
		Scene* m_Scene;
	};

}
