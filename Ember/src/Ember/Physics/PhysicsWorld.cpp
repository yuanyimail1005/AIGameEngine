#include "Ember/Physics/PhysicsWorld.h"

#include "Ember/Core/Log.h"
#include "Ember/Math/Math.h"
#include "Ember/Scene/Components.h"
#include "Ember/Scene/Entity.h"
#include "Ember/Scene/Scene.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace Ember {

	namespace {

		// ------------------------------------------------------------------------------------------
		// Process-wide Jolt initialization (reference counted: one per live PhysicsWorld).

		std::mutex s_JoltMutex;
		int s_JoltRefCount = 0;

		void JoltTrace(const char* format, ...)
		{
			char buffer[1024];
			va_list args;
			va_start(args, format);
			std::vsnprintf(buffer, sizeof(buffer), format, args);
			va_end(args);
			EMBER_CORE_TRACE("[Jolt] {}", buffer);
		}

#ifdef JPH_ENABLE_ASSERTS
		bool JoltAssertFailed(const char* expression, const char* message, const char* file, JPH::uint line)
		{
			EMBER_CORE_ERROR("[Jolt] Assertion failed: {} ({}) at {}:{}", expression, message ? message : "", file, line);
			return false; // do not break; Jolt continues
		}
#endif

		void AcquireJolt()
		{
			std::scoped_lock lock(s_JoltMutex);
			if (s_JoltRefCount++ == 0)
			{
				JPH::RegisterDefaultAllocator();
				JPH::Trace = JoltTrace;
				JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = JoltAssertFailed;)
				JPH::Factory::sInstance = new JPH::Factory();
				JPH::RegisterTypes();
			}
		}

		void ReleaseJolt()
		{
			std::scoped_lock lock(s_JoltMutex);
			if (--s_JoltRefCount == 0)
			{
				JPH::UnregisterTypes();
				delete JPH::Factory::sInstance;
				JPH::Factory::sInstance = nullptr;
			}
		}

		// ------------------------------------------------------------------------------------------
		// Layers

		namespace Layers {
			constexpr JPH::ObjectLayer NonMoving = 0;
			constexpr JPH::ObjectLayer Moving = 1;
			constexpr JPH::ObjectLayer Count = 2;
		}

		namespace BroadPhaseLayers {
			constexpr JPH::BroadPhaseLayer NonMoving(0);
			constexpr JPH::BroadPhaseLayer Moving(1);
			constexpr JPH::uint Count = 2;
		}

		class BroadPhaseLayerMapping final : public JPH::BroadPhaseLayerInterface
		{
		public:
			JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::Count; }

			JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
			{
				return layer == Layers::NonMoving ? BroadPhaseLayers::NonMoving : BroadPhaseLayers::Moving;
			}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
			{
				return layer == BroadPhaseLayers::NonMoving ? "NonMoving" : "Moving";
			}
#endif
		};

		class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override
			{
				// Static vs static never collides.
				return layer == Layers::Moving || broadPhaseLayer == BroadPhaseLayers::Moving;
			}
		};

		class ObjectPairFilter final : public JPH::ObjectLayerPairFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
			{
				return a == Layers::Moving || b == Layers::Moving;
			}
		};

		class IgnoreSensorsFilter final : public JPH::BodyFilter
		{
		public:
			bool ShouldCollideLocked(const JPH::Body& body) const override { return !body.IsSensor(); }
		};

		// ------------------------------------------------------------------------------------------
		// Conversions

		JPH::Vec3 ToJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
		JPH::Quat ToJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
		glm::vec3 ToGlm(const JPH::Vec3& v) { return { v.GetX(), v.GetY(), v.GetZ() }; }
		glm::quat ToGlm(const JPH::Quat& q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }
#ifdef JPH_DOUBLE_PRECISION
		JPH::RVec3 ToJoltR(const glm::vec3& v) { return JPH::RVec3(v.x, v.y, v.z); }
		glm::vec3 ToGlmR(const JPH::RVec3& v) { return { float(v.GetX()), float(v.GetY()), float(v.GetZ()) }; }
#else
		JPH::RVec3 ToJoltR(const glm::vec3& v) { return ToJolt(v); }
		glm::vec3 ToGlmR(const JPH::RVec3& v) { return ToGlm(v); }
#endif

		bool HasAnyCollider(Entity entity)
		{
			return entity.HasComponent<BoxColliderComponent>() || entity.HasComponent<SphereColliderComponent>() ||
				entity.HasComponent<CapsuleColliderComponent>();
		}

		uint64_t PairKey(JPH::BodyID a, JPH::BodyID b)
		{
			uint32_t x = a.GetIndexAndSequenceNumber();
			uint32_t y = b.GetIndexAndSequenceNumber();
			if (x > y)
				std::swap(x, y);
			return (uint64_t(x) << 32) | y;
		}

	}

	// ----------------------------------------------------------------------------------------------

	struct PhysicsWorld::Impl final : public JPH::ContactListener
	{
		struct BodyRecord
		{
			JPH::BodyID Body;
			RigidBodyType Type = RigidBodyType::Static;
			bool IsTrigger = false;
			glm::vec3 LastPosition{ 0.0f };
			glm::quat LastRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
			glm::vec3 LastScale{ 1.0f };
		};

		struct ContactPair
		{
			UUID A, B;
			int Count = 0;
			bool IsTrigger = false;
		};

		BroadPhaseLayerMapping BroadPhaseMapping;
		ObjectVsBroadPhaseFilter ObjectVsBroadPhase;
		ObjectPairFilter ObjectPair;
		Scope<JPH::TempAllocatorImpl> TempAllocator;
		Scope<JPH::JobSystemThreadPool> JobSystem;
		Scope<JPH::PhysicsSystem> System;

		std::unordered_map<UUID, BodyRecord> Bodies;
		std::unordered_map<uint32_t, UUID> BodyToEntity; // body index+sequence -> entity
		std::unordered_set<UUID> WarnedNoCollider;
		float Accumulator = 0.0f;

		// Written from Jolt worker threads during Update.
		std::mutex ContactMutex;
		std::map<uint64_t, ContactPair> ActiveContacts;
		std::vector<ContactEvent> PendingEvents;

		// --- JPH::ContactListener -----------------------------------------------------------------
		void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold&, JPH::ContactSettings&) override
		{
			const UUID a = static_cast<uint64_t>(body1.GetUserData());
			const UUID b = static_cast<uint64_t>(body2.GetUserData());
			const bool trigger = body1.IsSensor() || body2.IsSensor();
			std::scoped_lock lock(ContactMutex);
			ContactPair& pair = ActiveContacts[PairKey(body1.GetID(), body2.GetID())];
			if (pair.Count++ == 0)
			{
				pair.A = a;
				pair.B = b;
				pair.IsTrigger = trigger;
				PendingEvents.push_back(ContactEvent{ a, b, true, trigger });
			}
		}

		void OnContactRemoved(const JPH::SubShapeIDPair& subShapePair) override
		{
			std::scoped_lock lock(ContactMutex);
			auto it = ActiveContacts.find(PairKey(subShapePair.GetBody1ID(), subShapePair.GetBody2ID()));
			if (it == ActiveContacts.end())
				return;
			if (--it->second.Count <= 0)
			{
				PendingEvents.push_back(ContactEvent{ it->second.A, it->second.B, false, it->second.IsTrigger });
				ActiveContacts.erase(it);
			}
		}

		// Ends all contacts involving a body (called when the body is destroyed).
		void EndContactsForBody(JPH::BodyID body)
		{
			std::scoped_lock lock(ContactMutex);
			const uint32_t id = body.GetIndexAndSequenceNumber();
			for (auto it = ActiveContacts.begin(); it != ActiveContacts.end();)
			{
				const uint32_t low = static_cast<uint32_t>(it->first & 0xFFFFFFFFu);
				const uint32_t high = static_cast<uint32_t>(it->first >> 32);
				if (low == id || high == id)
				{
					PendingEvents.push_back(ContactEvent{ it->second.A, it->second.B, false, it->second.IsTrigger });
					it = ActiveContacts.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		JPH::BodyInterface& BodyInterface() { return System->GetBodyInterface(); }

		const BodyRecord* FindRecord(Entity entity) const
		{
			if (!entity)
				return nullptr;
			auto it = Bodies.find(entity.GetUUID());
			return it != Bodies.end() ? &it->second : nullptr;
		}
	};

	// ----------------------------------------------------------------------------------------------

	PhysicsWorld::PhysicsWorld(Scene* scene)
		: m_Impl(CreateScope<Impl>()), m_Scene(scene)
	{
		AcquireJolt();

		constexpr JPH::uint maxBodies = 65536;
		constexpr JPH::uint numBodyMutexes = 0; // default
		constexpr JPH::uint maxBodyPairs = 65536;
		constexpr JPH::uint maxContactConstraints = 16384;

		m_Impl->TempAllocator = CreateScope<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
		const int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
		m_Impl->JobSystem = CreateScope<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);
		m_Impl->System = CreateScope<JPH::PhysicsSystem>();
		m_Impl->System->Init(maxBodies, numBodyMutexes, maxBodyPairs, maxContactConstraints,
			m_Impl->BroadPhaseMapping, m_Impl->ObjectVsBroadPhase, m_Impl->ObjectPair);
		m_Impl->System->SetContactListener(m_Impl.get());
		m_Impl->System->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
	}

	PhysicsWorld::~PhysicsWorld()
	{
		OnRuntimeStop();
		m_Impl->System.reset();
		m_Impl->JobSystem.reset();
		m_Impl->TempAllocator.reset();
		m_Impl.reset();
		ReleaseJolt();
	}

	void PhysicsWorld::OnRuntimeStart()
	{
		for (Entity entity : m_Scene->GetAllEntitiesOrdered())
		{
			if (HasAnyCollider(entity))
				RecreateBody(entity);
		}
		m_Impl->System->OptimizeBroadPhase();
	}

	void PhysicsWorld::OnRuntimeStop()
	{
		if (!m_Impl || !m_Impl->System)
			return;
		JPH::BodyInterface& bodies = m_Impl->BodyInterface();
		for (auto& [uuid, record] : m_Impl->Bodies)
		{
			bodies.RemoveBody(record.Body);
			bodies.DestroyBody(record.Body);
		}
		m_Impl->Bodies.clear();
		m_Impl->BodyToEntity.clear();
		std::scoped_lock lock(m_Impl->ContactMutex);
		m_Impl->ActiveContacts.clear();
		m_Impl->PendingEvents.clear();
	}

	void PhysicsWorld::RecreateBody(Entity entity)
	{
		if (!entity)
			return;
		DestroyBody(entity);

		if (!HasAnyCollider(entity))
		{
			if (entity.HasComponent<RigidBodyComponent>() && m_Impl->WarnedNoCollider.insert(entity.GetUUID()).second)
				EMBER_CORE_WARN("Entity '{}' has a RigidBody but no collider - it will not be simulated", entity.GetName());
			return;
		}

		glm::vec3 position, scale;
		glm::quat rotation;
		Math::DecomposeTransform(m_Scene->GetWorldTransform(entity), position, rotation, scale);
		const glm::vec3 absScale = glm::max(glm::abs(scale), glm::vec3(1e-4f));

		// Build one sub-shape per collider (sizes baked with the entity's world scale).
		struct SubShape
		{
			JPH::RefConst<JPH::Shape> Shape;
			glm::vec3 Offset;
		};
		std::vector<SubShape> subShapes;

		if (const auto* box = entity.TryGetComponent<BoxColliderComponent>())
		{
			const glm::vec3 halfExtents = glm::max(box->HalfExtents * absScale, glm::vec3(0.005f));
			const float convexRadius = std::min(JPH::cDefaultConvexRadius, 0.5f * std::min({ halfExtents.x, halfExtents.y, halfExtents.z }));
			subShapes.push_back({ new JPH::BoxShape(ToJolt(halfExtents), convexRadius), box->Offset * absScale });
		}
		if (const auto* sphere = entity.TryGetComponent<SphereColliderComponent>())
		{
			const float radius = std::max(sphere->Radius * std::max({ absScale.x, absScale.y, absScale.z }), 0.005f);
			subShapes.push_back({ new JPH::SphereShape(radius), sphere->Offset * absScale });
		}
		if (const auto* capsule = entity.TryGetComponent<CapsuleColliderComponent>())
		{
			const float radius = std::max(capsule->Radius * std::max(absScale.x, absScale.z), 0.005f);
			const float halfHeight = std::max(capsule->HalfHeight * absScale.y, 0.005f);
			subShapes.push_back({ new JPH::CapsuleShape(halfHeight, radius), capsule->Offset * absScale });
		}

		JPH::RefConst<JPH::Shape> shape;
		if (subShapes.size() == 1 && subShapes[0].Offset == glm::vec3(0.0f))
		{
			shape = subShapes[0].Shape;
		}
		else
		{
			JPH::StaticCompoundShapeSettings compound;
			for (const SubShape& sub : subShapes)
				compound.AddShape(ToJolt(sub.Offset), JPH::Quat::sIdentity(), sub.Shape);
			JPH::ShapeSettings::ShapeResult result = compound.Create();
			if (result.HasError())
			{
				EMBER_CORE_ERROR("Failed to create collision shape for '{}': {}", entity.GetName(), result.GetError().c_str());
				return;
			}
			shape = result.Get();
		}

		const RigidBodyComponent defaults{ RigidBodyType::Static };
		const RigidBodyComponent* rigidBody = entity.TryGetComponent<RigidBodyComponent>();
		const RigidBodyComponent& rb = rigidBody ? *rigidBody : defaults;

		JPH::EMotionType motionType = JPH::EMotionType::Static;
		JPH::ObjectLayer layer = Layers::NonMoving;
		if (rb.Type == RigidBodyType::Dynamic)
		{
			motionType = JPH::EMotionType::Dynamic;
			layer = Layers::Moving;
		}
		else if (rb.Type == RigidBodyType::Kinematic)
		{
			motionType = JPH::EMotionType::Kinematic;
			layer = Layers::Moving;
		}
		// Triggers live in the moving layer so they also detect static and kinematic bodies.
		if (rb.IsTrigger)
			layer = Layers::Moving;

		JPH::BodyCreationSettings settings(shape, ToJoltR(position), ToJolt(glm::normalize(rotation)), motionType, layer);
		settings.mUserData = static_cast<uint64_t>(entity.GetUUID());
		settings.mFriction = std::max(rb.Friction, 0.0f);
		settings.mRestitution = std::clamp(rb.Restitution, 0.0f, 1.0f);
		settings.mLinearDamping = std::max(rb.LinearDamping, 0.0f);
		settings.mAngularDamping = std::max(rb.AngularDamping, 0.0f);
		settings.mGravityFactor = rb.GravityScale;
		settings.mIsSensor = rb.IsTrigger;
		settings.mCollideKinematicVsNonDynamic = rb.IsTrigger;
		if (motionType == JPH::EMotionType::Dynamic)
		{
			settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
			settings.mMassPropertiesOverride.mMass = std::max(rb.Mass, 0.001f);
			if (rb.LockRotation)
				settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
			if (rb.ContinuousDetection)
				settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
		}

		JPH::BodyInterface& bodies = m_Impl->BodyInterface();
		const JPH::EActivation activation = motionType == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
		const JPH::BodyID bodyID = bodies.CreateAndAddBody(settings, activation);
		if (bodyID.IsInvalid())
		{
			EMBER_CORE_ERROR("Physics body limit reached; cannot create body for '{}'", entity.GetName());
			return;
		}

		Impl::BodyRecord record;
		record.Body = bodyID;
		record.Type = rigidBody ? rb.Type : RigidBodyType::Static;
		record.IsTrigger = rb.IsTrigger;
		record.LastPosition = position;
		record.LastRotation = glm::normalize(rotation);
		record.LastScale = scale;
		m_Impl->Bodies[entity.GetUUID()] = record;
		m_Impl->BodyToEntity[bodyID.GetIndexAndSequenceNumber()] = entity.GetUUID();
	}

	void PhysicsWorld::DestroyBody(Entity entity)
	{
		if (!entity)
			return;
		auto it = m_Impl->Bodies.find(entity.GetUUID());
		if (it == m_Impl->Bodies.end())
			return;
		const JPH::BodyID body = it->second.Body;
		m_Impl->EndContactsForBody(body);
		JPH::BodyInterface& bodies = m_Impl->BodyInterface();
		bodies.RemoveBody(body);
		bodies.DestroyBody(body);
		m_Impl->BodyToEntity.erase(body.GetIndexAndSequenceNumber());
		m_Impl->Bodies.erase(it);
	}

	bool PhysicsWorld::HasBody(Entity entity) const
	{
		return m_Impl->FindRecord(entity) != nullptr;
	}

	size_t PhysicsWorld::GetBodyCount() const
	{
		return m_Impl->Bodies.size();
	}

	void PhysicsWorld::Step(Timestep ts)
	{
		JPH::BodyInterface& bodies = m_Impl->BodyInterface();

		// 1. Reconcile bodies with components: remove orphans, create bodies for new collider entities.
		std::vector<UUID> stale;
		for (const auto& [uuid, record] : m_Impl->Bodies)
		{
			Entity entity = m_Scene->GetEntityByUUID(uuid);
			if (!entity || !HasAnyCollider(entity))
				stale.push_back(uuid);
		}
		for (UUID uuid : stale)
		{
			auto it = m_Impl->Bodies.find(uuid);
			m_Impl->EndContactsForBody(it->second.Body);
			bodies.RemoveBody(it->second.Body);
			bodies.DestroyBody(it->second.Body);
			m_Impl->BodyToEntity.erase(it->second.Body.GetIndexAndSequenceNumber());
			m_Impl->Bodies.erase(it);
		}

		std::vector<Entity> toCreate;
		auto collectNew = [&](auto view)
		{
			for (entt::entity handle : view)
			{
				Entity entity(handle, m_Scene);
				if (!m_Impl->Bodies.contains(entity.GetUUID()) &&
					std::find(toCreate.begin(), toCreate.end(), entity) == toCreate.end())
					toCreate.push_back(entity);
			}
		};
		collectNew(m_Scene->GetAllEntitiesWith<BoxColliderComponent>());
		collectNew(m_Scene->GetAllEntitiesWith<SphereColliderComponent>());
		collectNew(m_Scene->GetAllEntitiesWith<CapsuleColliderComponent>());
		for (Entity entity : toCreate)
			RecreateBody(entity);

		// 2. Push transform edits (scripts / editor) into the simulation.
		std::vector<UUID> rescaled;
		struct KinematicTarget
		{
			JPH::BodyID Body;
			glm::vec3 Position;
			glm::quat Rotation;
		};
		std::vector<KinematicTarget> kinematicTargets;
		for (auto& [uuid, record] : m_Impl->Bodies)
		{
			Entity entity = m_Scene->GetEntityByUUID(uuid);
			glm::vec3 position, scale;
			glm::quat rotation;
			Math::DecomposeTransform(m_Scene->GetWorldTransform(entity), position, rotation, scale);
			rotation = glm::normalize(rotation);

			if (!Math::NearlyEqual(scale, record.LastScale, 1e-4f))
			{
				rescaled.push_back(uuid);
				continue;
			}

			const bool moved = !Math::NearlyEqual(position, record.LastPosition, 1e-5f) ||
				std::abs(glm::dot(rotation, record.LastRotation)) < 0.999999f;
			if (record.Type == RigidBodyType::Kinematic)
			{
				kinematicTargets.push_back({ record.Body, position, rotation });
			}
			else if (moved)
			{
				bodies.SetPositionAndRotation(record.Body, ToJoltR(position), ToJolt(rotation),
					record.Type == RigidBodyType::Dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
				record.LastPosition = position;
				record.LastRotation = rotation;
			}
		}
		for (UUID uuid : rescaled)
			RecreateBody(m_Scene->GetEntityByUUID(uuid));

		// 3. Fixed-step simulation.
		m_Impl->Accumulator += std::max(ts.GetSeconds(), 0.0f);
		int steps = 0;
		while (m_Impl->Accumulator >= c_FixedTimestep && steps < c_MaxSubSteps)
		{
			for (const KinematicTarget& target : kinematicTargets)
				bodies.MoveKinematic(target.Body, ToJoltR(target.Position), ToJolt(target.Rotation), c_FixedTimestep);

			const JPH::EPhysicsUpdateError error = m_Impl->System->Update(c_FixedTimestep, 1,
				m_Impl->TempAllocator.get(), m_Impl->JobSystem.get());
			if (error != JPH::EPhysicsUpdateError::None)
				EMBER_CORE_WARN("Physics update reported error flags {}", static_cast<int>(error));

			m_Impl->Accumulator -= c_FixedTimestep;
			steps++;
		}
		if (steps == c_MaxSubSteps)
			m_Impl->Accumulator = std::min(m_Impl->Accumulator, c_FixedTimestep); // drop backlog (avoid spiral of death)

		// 4. Write simulated poses back to entities.
		for (auto& [uuid, record] : m_Impl->Bodies)
		{
			if (record.Type == RigidBodyType::Kinematic)
			{
				for (const KinematicTarget& target : kinematicTargets)
				{
					if (target.Body == record.Body)
					{
						record.LastPosition = target.Position;
						record.LastRotation = target.Rotation;
					}
				}
				continue;
			}
			if (record.Type != RigidBodyType::Dynamic)
				continue;

			JPH::RVec3 joltPosition;
			JPH::Quat joltRotation;
			bodies.GetPositionAndRotation(record.Body, joltPosition, joltRotation);
			const glm::vec3 position = ToGlmR(joltPosition);
			const glm::quat rotation = glm::normalize(ToGlm(joltRotation));
			if (Math::NearlyEqual(position, record.LastPosition, 1e-7f) && std::abs(glm::dot(rotation, record.LastRotation)) > 0.9999999f)
				continue;

			Entity entity = m_Scene->GetEntityByUUID(uuid);
			m_Scene->SetWorldTransform(entity, Math::ComposeTransform(position, rotation, record.LastScale));
			record.LastPosition = position;
			record.LastRotation = rotation;
		}
	}

	namespace {

		bool IsMovable(RigidBodyType type) { return type != RigidBodyType::Static; }

	}

	void PhysicsWorld::AddForce(Entity entity, const glm::vec3& force)
	{
		if (const auto* record = m_Impl->FindRecord(entity); record && record->Type == RigidBodyType::Dynamic)
			m_Impl->BodyInterface().AddForce(record->Body, ToJolt(force));
	}

	void PhysicsWorld::AddImpulse(Entity entity, const glm::vec3& impulse)
	{
		if (const auto* record = m_Impl->FindRecord(entity); record && record->Type == RigidBodyType::Dynamic)
			m_Impl->BodyInterface().AddImpulse(record->Body, ToJolt(impulse));
	}

	void PhysicsWorld::AddTorque(Entity entity, const glm::vec3& torque)
	{
		if (const auto* record = m_Impl->FindRecord(entity); record && record->Type == RigidBodyType::Dynamic)
			m_Impl->BodyInterface().AddTorque(record->Body, ToJolt(torque));
	}

	void PhysicsWorld::SetLinearVelocity(Entity entity, const glm::vec3& velocity)
	{
		if (const auto* record = m_Impl->FindRecord(entity); record && IsMovable(record->Type))
		{
			m_Impl->BodyInterface().SetLinearVelocity(record->Body, ToJolt(velocity));
			m_Impl->BodyInterface().ActivateBody(record->Body);
		}
	}

	glm::vec3 PhysicsWorld::GetLinearVelocity(Entity entity) const
	{
		if (const auto* record = m_Impl->FindRecord(entity))
			return ToGlm(m_Impl->BodyInterface().GetLinearVelocity(record->Body));
		return glm::vec3(0.0f);
	}

	void PhysicsWorld::SetAngularVelocity(Entity entity, const glm::vec3& velocity)
	{
		if (const auto* record = m_Impl->FindRecord(entity); record && IsMovable(record->Type))
		{
			m_Impl->BodyInterface().SetAngularVelocity(record->Body, ToJolt(velocity));
			m_Impl->BodyInterface().ActivateBody(record->Body);
		}
	}

	glm::vec3 PhysicsWorld::GetAngularVelocity(Entity entity) const
	{
		if (const auto* record = m_Impl->FindRecord(entity))
			return ToGlm(m_Impl->BodyInterface().GetAngularVelocity(record->Body));
		return glm::vec3(0.0f);
	}

	void PhysicsWorld::SetGravity(const glm::vec3& gravity)
	{
		m_Impl->System->SetGravity(ToJolt(gravity));
		// Wake everything up so sleeping bodies respond to the new gravity.
		JPH::BodyIDVector ids;
		m_Impl->System->GetBodies(ids);
		m_Impl->BodyInterface().ActivateBodies(ids.data(), static_cast<int>(ids.size()));
	}

	glm::vec3 PhysicsWorld::GetGravity() const
	{
		return ToGlm(m_Impl->System->GetGravity());
	}

	std::optional<RaycastHit> PhysicsWorld::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const
	{
		const float length = glm::length(direction);
		if (length < 1e-8f || maxDistance <= 0.0f)
			return std::nullopt;
		const glm::vec3 dir = direction / length;

		JPH::RRayCast ray{ ToJoltR(origin), ToJolt(dir * maxDistance) };
		JPH::RayCastResult result;
		IgnoreSensorsFilter bodyFilter;
		if (!m_Impl->System->GetNarrowPhaseQuery().CastRay(ray, result, {}, {}, bodyFilter))
			return std::nullopt;

		RaycastHit hit;
		hit.Distance = result.mFraction * maxDistance;
		hit.Point = origin + dir * hit.Distance;
		auto it = m_Impl->BodyToEntity.find(result.mBodyID.GetIndexAndSequenceNumber());
		hit.EntityID = it != m_Impl->BodyToEntity.end() ? it->second : UUID(0);

		JPH::BodyLockRead lock(m_Impl->System->GetBodyLockInterface(), result.mBodyID);
		if (lock.Succeeded())
			hit.Normal = ToGlm(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, ToJoltR(hit.Point)));
		return hit;
	}

	std::vector<ContactEvent> PhysicsWorld::ConsumeContactEvents()
	{
		std::scoped_lock lock(m_Impl->ContactMutex);
		std::vector<ContactEvent> events;
		events.swap(m_Impl->PendingEvents);
		return events;
	}

}
