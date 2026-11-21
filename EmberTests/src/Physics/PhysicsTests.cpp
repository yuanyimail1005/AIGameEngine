#include <doctest/doctest.h>

#include "Ember/Physics/PhysicsWorld.h"
#include "Ember/Scene/Entity.h"
#include "Ember/Scene/Scene.h"

using namespace Ember;

namespace {

	Entity CreateGround(Scene& scene)
	{
		Entity ground = scene.CreateEntity("Ground");
		ground.AddComponent<BoxColliderComponent>().HalfExtents = { 50.0f, 0.5f, 50.0f };
		ground.GetComponent<TransformComponent>().Translation = { 0.0f, -0.5f, 0.0f };
		return ground;
	}

	Entity CreateBall(Scene& scene, const glm::vec3& position, float radius = 0.5f)
	{
		Entity ball = scene.CreateEntity("Ball");
		ball.GetComponent<TransformComponent>().Translation = position;
		ball.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Dynamic;
		ball.AddComponent<SphereColliderComponent>().Radius = radius;
		return ball;
	}

	void Simulate(Scene& scene, float seconds)
	{
		const int frames = static_cast<int>(seconds * 60.0f);
		for (int i = 0; i < frames; i++)
			scene.OnUpdateRuntime(Timestep(1.0f / 60.0f));
	}

}

TEST_SUITE("Physics")
{
	TEST_CASE("Physics: dynamic body falls under gravity and rests on static ground")
	{
		Scene scene;
		CreateGround(scene);
		Entity ball = CreateBall(scene, { 0.0f, 5.0f, 0.0f });
		scene.OnRuntimeStart();
		REQUIRE(scene.GetPhysicsWorld());
		CHECK(scene.GetPhysicsWorld()->GetBodyCount() == 2);

		Simulate(scene, 0.5f);
		const float midY = ball.GetComponent<TransformComponent>().Translation.y;
		CHECK(midY < 5.0f);
		CHECK(midY > 0.5f);

		Simulate(scene, 3.0f);
		CHECK(ball.GetComponent<TransformComponent>().Translation.y == doctest::Approx(0.5f).epsilon(0.03));
		scene.OnRuntimeStop();
		CHECK(scene.GetPhysicsWorld() == nullptr);
	}

	TEST_CASE("Physics: kinematic and static bodies are not moved by gravity; collider-only entity is static")
	{
		Scene scene;
		Entity kinematic = scene.CreateEntity("Kinematic");
		kinematic.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Kinematic;
		kinematic.AddComponent<BoxColliderComponent>();
		kinematic.GetComponent<TransformComponent>().Translation = { 5.0f, 5.0f, 0.0f };
		Entity colliderOnly = scene.CreateEntity("Wall");
		colliderOnly.AddComponent<BoxColliderComponent>();
		colliderOnly.GetComponent<TransformComponent>().Translation = { -5.0f, 5.0f, 0.0f };
		Entity noCollider = scene.CreateEntity("NoCollider");
		noCollider.AddComponent<RigidBodyComponent>();

		scene.OnRuntimeStart();
		CHECK(scene.GetPhysicsWorld()->HasBody(kinematic));
		CHECK(scene.GetPhysicsWorld()->HasBody(colliderOnly));
		CHECK_FALSE(scene.GetPhysicsWorld()->HasBody(noCollider));
		Simulate(scene, 1.0f);
		CHECK(kinematic.GetComponent<TransformComponent>().Translation.y == doctest::Approx(5.0f));
		CHECK(colliderOnly.GetComponent<TransformComponent>().Translation.y == doctest::Approx(5.0f));

		// Moving a kinematic body through its transform is followed by the simulation.
		kinematic.GetComponent<TransformComponent>().Translation = { 6.0f, 5.0f, 0.0f };
		Simulate(scene, 0.1f);
		CHECK(kinematic.GetComponent<TransformComponent>().Translation.x == doctest::Approx(6.0f));
		scene.OnRuntimeStop();
	}

	TEST_CASE("Physics: velocity, impulses, gravity changes and teleporting through the transform")
	{
		Scene scene;
		Entity ball = CreateBall(scene, { 0.0f, 0.0f, 0.0f });
		ball.GetComponent<RigidBodyComponent>().GravityScale = 0.0f;
		ball.GetComponent<RigidBodyComponent>().LinearDamping = 0.0f;
		scene.OnRuntimeStart();
		PhysicsWorld& physics = *scene.GetPhysicsWorld();

		physics.SetLinearVelocity(ball, { 2.0f, 0.0f, 0.0f });
		Simulate(scene, 1.0f);
		CHECK(ball.GetComponent<TransformComponent>().Translation.x == doctest::Approx(2.0f).epsilon(0.02));
		CHECK(physics.GetLinearVelocity(ball).x == doctest::Approx(2.0f).epsilon(0.01));

		physics.SetLinearVelocity(ball, glm::vec3(0.0f));
		physics.AddImpulse(ball, { 0.0f, 0.0f, 3.0f }); // mass 1 -> 3 m/s
		Simulate(scene, 1.0f / 60.0f);
		CHECK(physics.GetLinearVelocity(ball).z == doctest::Approx(3.0f).epsilon(0.01));

		// Teleport via transform.
		ball.GetComponent<TransformComponent>().Translation = { 100.0f, 0.0f, 0.0f };
		physics.SetLinearVelocity(ball, glm::vec3(0.0f));
		Simulate(scene, 1.0f / 60.0f);
		CHECK(ball.GetComponent<TransformComponent>().Translation.x == doctest::Approx(100.0f).epsilon(0.001));

		physics.SetGravity({ 0.0f, 0.0f, 0.0f });
		CHECK(physics.GetGravity() == glm::vec3(0.0f));
		scene.OnRuntimeStop();
	}

	TEST_CASE("Physics: collision and trigger events are reported once per pair")
	{
		Scene scene;
		Entity ground = CreateGround(scene);
		Entity ball = CreateBall(scene, { 0.0f, 2.0f, 0.0f });
		Entity trigger = scene.CreateEntity("Trigger");
		trigger.GetComponent<TransformComponent>().Translation = { 0.0f, 4.0f, 0.0f };
		auto& triggerBody = trigger.AddComponent<RigidBodyComponent>();
		triggerBody.Type = RigidBodyType::Static;
		triggerBody.IsTrigger = true;
		trigger.AddComponent<BoxColliderComponent>().HalfExtents = { 1.0f, 1.0f, 1.0f };
		Entity faller = CreateBall(scene, { 0.0f, 8.0f, 0.0f }); // falls through the trigger onto the ground
		ball.GetComponent<TransformComponent>().Translation = { 5.0f, 2.0f, 0.0f }; // lands beside the trigger

		scene.OnRuntimeStart();
		int collisionBegins = 0, triggerBegins = 0, triggerEnds = 0;
		// Step physics directly: Scene::OnUpdateRuntime would hand the events to the script engine.
		for (int frame = 0; frame < 240; frame++)
		{
			scene.GetPhysicsWorld()->Step(Timestep(1.0f / 60.0f));
			for (const ContactEvent& event : scene.GetPhysicsWorld()->ConsumeContactEvents())
			{
				const bool involvesTrigger = event.EntityA == trigger.GetUUID() || event.EntityB == trigger.GetUUID();
				if (event.IsTrigger)
				{
					CHECK(involvesTrigger);
					(event.Begin ? triggerBegins : triggerEnds)++;
				}
				else if (event.Begin && (event.EntityA == ground.GetUUID() || event.EntityB == ground.GetUUID()))
				{
					collisionBegins++;
				}
			}
		}
		CHECK(triggerBegins == 1);
		CHECK(triggerEnds == 1);
		CHECK(collisionBegins == 2); // both balls land on the ground
		CHECK(faller.GetComponent<TransformComponent>().Translation.y == doctest::Approx(0.5f).epsilon(0.05));
		scene.OnRuntimeStop();
	}

	TEST_CASE("Physics: raycast hits the closest non-trigger body")
	{
		Scene scene;
		Entity ground = CreateGround(scene);
		Entity box = scene.CreateEntity("Box");
		box.AddComponent<BoxColliderComponent>();
		box.GetComponent<TransformComponent>().Translation = { 0.0f, 3.0f, 0.0f };
		Entity trigger = scene.CreateEntity("Trigger");
		trigger.AddComponent<RigidBodyComponent>().IsTrigger = true;
		trigger.GetComponent<RigidBodyComponent>().Type = RigidBodyType::Static;
		trigger.AddComponent<BoxColliderComponent>();
		trigger.GetComponent<TransformComponent>().Translation = { 0.0f, 6.0f, 0.0f };

		scene.OnRuntimeStart();
		auto hit = scene.GetPhysicsWorld()->Raycast({ 0.0f, 10.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 100.0f);
		REQUIRE(hit.has_value());
		CHECK(hit->EntityID == box.GetUUID());
		CHECK(hit->Point.y == doctest::Approx(3.5f).epsilon(0.01));
		CHECK(hit->Normal.y == doctest::Approx(1.0f).epsilon(0.01));
		CHECK(hit->Distance == doctest::Approx(6.5f).epsilon(0.01));

		auto groundHit = scene.GetPhysicsWorld()->Raycast({ 5.0f, 10.0f, 0.0f }, { 0.0f, -2.0f, 0.0f }, 100.0f);
		REQUIRE(groundHit.has_value());
		CHECK(groundHit->EntityID == ground.GetUUID());

		CHECK_FALSE(scene.GetPhysicsWorld()->Raycast({ 0.0f, 10.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 100.0f).has_value());
		CHECK_FALSE(scene.GetPhysicsWorld()->Raycast({ 0.0f, 10.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 100.0f).has_value());
		scene.OnRuntimeStop();
	}

	TEST_CASE("Physics: bodies are created for entities spawned at runtime and removed when destroyed")
	{
		Scene scene;
		CreateGround(scene);
		scene.OnRuntimeStart();
		PhysicsWorld& physics = *scene.GetPhysicsWorld();
		CHECK(physics.GetBodyCount() == 1);

		Entity spawned = CreateBall(scene, { 0.0f, 3.0f, 0.0f });
		scene.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK(physics.HasBody(spawned));
		CHECK(physics.GetBodyCount() == 2);

		// Removing the collider removes the body.
		spawned.RemoveComponent<SphereColliderComponent>();
		scene.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK_FALSE(physics.HasBody(spawned));

		spawned.AddComponent<BoxColliderComponent>();
		scene.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK(physics.HasBody(spawned));

		scene.DestroyEntity(spawned); // deferred while running
		CHECK(spawned.IsValid());
		scene.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK_FALSE(spawned.IsValid());
		CHECK(physics.GetBodyCount() == 1);
		scene.OnRuntimeStop();
	}

	TEST_CASE("Physics: world scale is applied to collider sizes")
	{
		Scene scene;
		Entity box = scene.CreateEntity("BigBox");
		box.AddComponent<BoxColliderComponent>(); // half extents 0.5
		box.GetComponent<TransformComponent>().Scale = { 4.0f, 4.0f, 4.0f };
		scene.OnRuntimeStart();
		auto hit = scene.GetPhysicsWorld()->Raycast({ 0.0f, 10.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 100.0f);
		REQUIRE(hit.has_value());
		CHECK(hit->Point.y == doctest::Approx(2.0f).epsilon(0.01));
		scene.OnRuntimeStop();
	}
}
