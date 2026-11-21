#include <doctest/doctest.h>

#include "Ember/Math/Math.h"
#include "Ember/Scene/ComponentRegistry.h"
#include "Ember/Scene/Entity.h"
#include "Ember/Scene/Scene.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace Ember;

TEST_SUITE("Scene")
{
	TEST_CASE("Scene: entities get ID, tag, transform and relationship components")
	{
		Scene scene("Test");
		Entity entity = scene.CreateEntity("Player");
		REQUIRE(entity);
		CHECK(entity.HasComponent<IDComponent>());
		CHECK(entity.HasComponent<TagComponent>());
		CHECK(entity.HasComponent<TransformComponent>());
		CHECK(entity.HasComponent<RelationshipComponent>());
		CHECK(entity.GetName() == "Player");
		CHECK(entity.GetUUID().IsValid());
		CHECK(scene.GetEntityCount() == 1);
		CHECK(scene.GetEntityByUUID(entity.GetUUID()) == entity);
		CHECK(scene.FindEntityByName("Player") == entity);
		CHECK_FALSE(scene.FindEntityByName("Nobody"));

		Entity unnamed = scene.CreateEntity();
		CHECK(unnamed.GetName() == "Entity");
	}

	TEST_CASE("Scene: CreateEntityWithUUID rejects duplicates and zero")
	{
		Scene scene;
		Entity a = scene.CreateEntityWithUUID(1234, "A");
		CHECK(static_cast<uint64_t>(a.GetUUID()) == 1234);
		Entity b = scene.CreateEntityWithUUID(1234, "B");
		CHECK(static_cast<uint64_t>(b.GetUUID()) != 1234);
		Entity c = scene.CreateEntityWithUUID(0, "C");
		CHECK(c.GetUUID().IsValid());
		CHECK(scene.GetEntityCount() == 3);
	}

	TEST_CASE("Scene: entities are ordered by creation and FindEntityByName is deterministic")
	{
		Scene scene;
		Entity first = scene.CreateEntity("Same");
		scene.CreateEntity("Other");
		scene.CreateEntity("Same");
		CHECK(scene.FindEntityByName("Same") == first);

		auto ordered = scene.GetAllEntitiesOrdered();
		REQUIRE(ordered.size() == 3);
		CHECK(ordered[0] == first);
		CHECK(ordered[1].GetName() == "Other");
	}

	TEST_CASE("Scene: hierarchy, world transforms and cycle prevention")
	{
		Scene scene;
		Entity parent = scene.CreateEntity("Parent");
		Entity child = scene.CreateEntity("Child");
		Entity grandchild = scene.CreateEntity("Grandchild");

		parent.GetComponent<TransformComponent>().Translation = { 10.0f, 0.0f, 0.0f };
		child.GetComponent<TransformComponent>().Translation = { 0.0f, 5.0f, 0.0f };

		// Keep world transform: child's local becomes relative to the parent.
		REQUIRE(scene.SetParent(child, parent, true));
		CHECK(Math::NearlyEqual(child.GetComponent<TransformComponent>().Translation, glm::vec3(-10.0f, 5.0f, 0.0f)));
		CHECK(Math::NearlyEqual(glm::vec3(scene.GetWorldTransform(child)[3]), glm::vec3(0.0f, 5.0f, 0.0f)));

		REQUIRE(scene.SetParent(grandchild, child, false));
		CHECK(scene.GetParent(grandchild) == child);
		CHECK(scene.IsDescendantOf(grandchild, parent));
		CHECK(scene.GetChildren(parent).size() == 1);

		// Cycles are rejected.
		CHECK_FALSE(scene.SetParent(parent, grandchild));
		CHECK_FALSE(scene.SetParent(parent, parent));

		auto roots = scene.GetRootEntities();
		REQUIRE(roots.size() == 1);
		CHECK(roots[0] == parent);

		// Rotated parent: world position of the child rotates with it.
		parent.GetComponent<TransformComponent>().Rotation = { 0.0f, glm::radians(90.0f), 0.0f };
		child.GetComponent<TransformComponent>().Translation = { 1.0f, 0.0f, 0.0f };
		const glm::vec3 world(scene.GetWorldTransform(child)[3]);
		CHECK(Math::NearlyEqual(world, glm::vec3(10.0f, 0.0f, -1.0f), 1e-4f));

		// SetWorldTransform inverts the parent chain.
		const glm::mat4 target = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 4.0f, 5.0f));
		scene.SetWorldTransform(grandchild, target);
		CHECK(Math::NearlyEqual(glm::vec3(scene.GetWorldTransform(grandchild)[3]), glm::vec3(3.0f, 4.0f, 5.0f), 1e-4f));

		// Unparent keeping world position.
		REQUIRE(scene.SetParent(grandchild, Entity(), true));
		CHECK_FALSE(scene.GetParent(grandchild));
		CHECK(Math::NearlyEqual(grandchild.GetComponent<TransformComponent>().Translation, glm::vec3(3.0f, 4.0f, 5.0f), 1e-4f));
		CHECK(scene.GetChildren(child).empty());
	}

	TEST_CASE("Scene: destroying an entity destroys its children and detaches from parent")
	{
		Scene scene;
		Entity root = scene.CreateEntity("Root");
		Entity a = scene.CreateEntity("A");
		Entity b = scene.CreateEntity("B");
		Entity c = scene.CreateEntity("C");
		scene.SetParent(a, root);
		scene.SetParent(b, a);
		scene.SetParent(c, root);
		const UUID bID = b.GetUUID();

		scene.DestroyEntity(a);
		CHECK_FALSE(a.IsValid());
		CHECK_FALSE(scene.HasEntity(bID));
		CHECK(scene.GetEntityCount() == 2);
		const auto& children = root.GetComponent<RelationshipComponent>().Children;
		REQUIRE(children.size() == 1);
		CHECK(children[0] == c.GetUUID());
	}

	TEST_CASE("Scene: duplicate copies components and children with fresh UUIDs")
	{
		Scene scene;
		Entity parent = scene.CreateEntity("Group");
		Entity original = scene.CreateEntity("Box");
		Entity child = scene.CreateEntity("BoxChild");
		scene.SetParent(original, parent);
		scene.SetParent(child, original);
		original.AddComponent<MeshRendererComponent>().Mesh = "builtin://Cube";
		original.AddComponent<RigidBodyComponent>().Mass = 7.0f;
		original.GetComponent<TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };

		Entity copy = scene.DuplicateEntity(original);
		REQUIRE(copy);
		CHECK(copy.GetUUID() != original.GetUUID());
		CHECK(copy.GetName() == "Box");
		CHECK(copy.GetComponent<MeshRendererComponent>().Mesh == "builtin://Cube");
		CHECK(copy.GetComponent<RigidBodyComponent>().Mass == doctest::Approx(7.0f));
		CHECK(scene.GetParent(copy) == parent);
		CHECK(parent.GetComponent<RelationshipComponent>().Children.size() == 2);

		auto copyChildren = scene.GetChildren(copy);
		REQUIRE(copyChildren.size() == 1);
		CHECK(copyChildren[0].GetName() == "BoxChild");
		CHECK(copyChildren[0] != child);
		CHECK(scene.GetChildren(original).size() == 1); // original untouched
		CHECK(scene.GetEntityCount() == 5);
	}

	TEST_CASE("Scene: Copy produces an independent deep copy with identical UUIDs")
	{
		Ref<Scene> scene = CreateRef<Scene>("Original");
		Entity a = scene->CreateEntity("A");
		Entity b = scene->CreateEntity("B");
		scene->SetParent(b, a);
		a.AddComponent<CameraComponent>().Primary = true;
		b.AddComponent<PointLightComponent>().Intensity = 42.0f;

		Ref<Scene> copy = Scene::Copy(scene);
		CHECK(copy->GetName() == "Original");
		CHECK(copy->GetEntityCount() == 2);
		Entity copyA = copy->GetEntityByUUID(a.GetUUID());
		Entity copyB = copy->GetEntityByUUID(b.GetUUID());
		REQUIRE(copyA);
		REQUIRE(copyB);
		CHECK(copy->GetParent(copyB) == copyA);
		CHECK(copyB.GetComponent<PointLightComponent>().Intensity == doctest::Approx(42.0f));
		CHECK(copy->GetPrimaryCameraEntity() == copyA);

		copyB.GetComponent<PointLightComponent>().Intensity = 1.0f;
		CHECK(b.GetComponent<PointLightComponent>().Intensity == doctest::Approx(42.0f));

		// Ordering preserved.
		auto ordered = copy->GetAllEntitiesOrdered();
		CHECK(ordered[0].GetName() == "A");
		CHECK(ordered[1].GetName() == "B");
	}

	TEST_CASE("Scene: primary camera and viewport resize update aspect ratios")
	{
		Scene scene;
		Entity secondary = scene.CreateEntity("Secondary");
		secondary.AddComponent<CameraComponent>().Primary = false;
		Entity primary = scene.CreateEntity("Primary");
		primary.AddComponent<CameraComponent>();
		Entity fixed = scene.CreateEntity("Fixed");
		auto& fixedCamera = fixed.AddComponent<CameraComponent>();
		fixedCamera.Primary = false;
		fixedCamera.FixedAspectRatio = true;

		CHECK(scene.GetPrimaryCameraEntity() == primary);
		scene.OnViewportResize(1000, 500);
		CHECK(primary.GetComponent<CameraComponent>().Camera.GetAspectRatio() == doctest::Approx(2.0f));
		CHECK(fixed.GetComponent<CameraComponent>().Camera.GetAspectRatio() == doctest::Approx(16.0f / 9.0f));
		CHECK(scene.GetViewportWidth() == 1000);
	}

	TEST_CASE("SceneCamera: projections map points as expected")
	{
		SceneCamera camera;
		camera.SetViewportSize(100, 100);
		camera.SetPerspective(glm::radians(90.0f), 1.0f, 100.0f);
		// A point on the near plane maps to depth 0, on the far plane to depth 1 (zero-to-one depth range).
		glm::vec4 nearPoint = camera.GetProjection() * glm::vec4(0.0f, 0.0f, -1.0f, 1.0f);
		glm::vec4 farPoint = camera.GetProjection() * glm::vec4(0.0f, 0.0f, -100.0f, 1.0f);
		CHECK(nearPoint.z / nearPoint.w == doctest::Approx(0.0f).epsilon(1e-4));
		CHECK(farPoint.z / farPoint.w == doctest::Approx(1.0f).epsilon(1e-4));
		// +Y in view space maps to +Y in clip space (no flip).
		glm::vec4 up = camera.GetProjection() * glm::vec4(0.0f, 0.5f, -1.0f, 1.0f);
		CHECK(up.y / up.w > 0.0f);

		camera.SetOrthographic(10.0f, -1.0f, 1.0f);
		glm::vec4 edge = camera.GetProjection() * glm::vec4(5.0f, 5.0f, 0.0f, 1.0f);
		CHECK(edge.x == doctest::Approx(1.0f));
		CHECK(edge.y == doctest::Approx(1.0f));
	}

	TEST_CASE("Math: decompose / compose round trip including negative scale")
	{
		const glm::vec3 translation(1.0f, -2.0f, 3.5f);
		const glm::quat rotation = glm::normalize(glm::quat(glm::vec3(0.3f, -1.1f, 0.7f)));
		for (const glm::vec3& scale : { glm::vec3(1.0f), glm::vec3(2.0f, 0.5f, 3.0f), glm::vec3(-1.0f, 2.0f, 1.0f) })
		{
			const glm::mat4 m = Math::ComposeTransform(translation, rotation, scale);
			glm::vec3 t, s;
			glm::quat r;
			REQUIRE(Math::DecomposeTransform(m, t, r, s));
			CHECK(Math::NearlyEqual(Math::ComposeTransform(t, r, s), m, 1e-4f));
			CHECK(Math::NearlyEqual(t, translation));
		}
		glm::vec3 t, r, s;
		CHECK_FALSE(Math::DecomposeTransform(glm::mat4(0.0f), t, r, s));
	}

	TEST_CASE("ComponentRegistry: lookup, add/remove and patch semantics")
	{
		Scene scene;
		Entity entity = scene.CreateEntity("E");

		CHECK(ComponentRegistry::Find("Transform") != nullptr);
		CHECK(ComponentRegistry::Find("transformcomponent") != nullptr);
		CHECK(ComponentRegistry::Find("MeshRendererComponent") == ComponentRegistry::Find("MeshRenderer"));
		CHECK(ComponentRegistry::Find("Bogus") == nullptr);

		const ComponentInfo* rigidBody = ComponentRegistry::Find("RigidBody");
		REQUIRE(rigidBody);
		CHECK_FALSE(rigidBody->Has(entity));
		REQUIRE(rigidBody->Deserialize(entity, YAML::Load("{Type: Kinematic, Mass: 3.5}")));
		CHECK(entity.GetComponent<RigidBodyComponent>().Type == RigidBodyType::Kinematic);
		CHECK(entity.GetComponent<RigidBodyComponent>().Mass == doctest::Approx(3.5f));
		// Patch leaves other fields alone.
		REQUIRE(rigidBody->Deserialize(entity, YAML::Load("{Friction: 0.9}")));
		CHECK(entity.GetComponent<RigidBodyComponent>().Mass == doctest::Approx(3.5f));
		CHECK(entity.GetComponent<RigidBodyComponent>().Friction == doctest::Approx(0.9f));
		// Bad values are reported (and do not crash).
		CHECK_FALSE(rigidBody->Deserialize(entity, YAML::Load("{Type: Wobbly}")));
		CHECK_FALSE(rigidBody->Deserialize(entity, YAML::Load("{Mass: [1, 2]}")));
		CHECK(entity.GetComponent<RigidBodyComponent>().Type == RigidBodyType::Kinematic);
		rigidBody->Remove(entity);
		CHECK_FALSE(entity.HasComponent<RigidBodyComponent>());

		// JSON input works too (JSON is YAML), and degrees convenience.
		const ComponentInfo* transform = ComponentRegistry::Find("Transform");
		REQUIRE(transform->Deserialize(entity, YAML::Load(R"({"Translation": [1, 2, 3], "RotationDegrees": [0, 90, 0]})")));
		CHECK(Math::NearlyEqual(entity.GetComponent<TransformComponent>().Translation, glm::vec3(1.0f, 2.0f, 3.0f)));
		CHECK(entity.GetComponent<TransformComponent>().Rotation.y == doctest::Approx(glm::radians(90.0f)));

		// Material override patch enables the override and accepts RGB.
		const ComponentInfo* mesh = ComponentRegistry::Find("MeshRenderer");
		REQUIRE(mesh->Deserialize(entity, YAML::Load("{Mesh: 'builtin://Sphere', MaterialOverride: {BaseColor: [1, 0, 0], Metallic: 1}}")));
		const auto& renderer = entity.GetComponent<MeshRendererComponent>();
		CHECK(renderer.OverrideMaterial);
		CHECK(renderer.MaterialOverride.BaseColor == glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
		CHECK(renderer.MaterialOverride.Metallic == doctest::Approx(1.0f));

		// Every registered component serializes to a map and re-applies to an identical value.
		for (const ComponentInfo& info : ComponentRegistry::GetAll())
		{
			CAPTURE(info.Name);
			Entity target = scene.CreateEntity("Target");
			info.Add(target);
			CHECK(info.Has(target));
			const YAML::Node node = info.Serialize(target);
			CHECK(node.IsMap());
			Entity other = scene.CreateEntity("Other");
			CHECK(info.Deserialize(other, node));
			CHECK(YAML::Dump(info.Serialize(other)) == YAML::Dump(node));
		}
	}
}
