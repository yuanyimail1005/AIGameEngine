#include <doctest/doctest.h>

#include "Ember/Core/FileSystem.h"
#include "Ember/Scene/ComponentRegistry.h"
#include "Ember/Scene/Entity.h"
#include "Ember/Scene/SceneSerializer.h"

#include <filesystem>

using namespace Ember;

namespace {

	Ref<Scene> BuildSampleScene()
	{
		Ref<Scene> scene = CreateRef<Scene>("Sample");
		Entity camera = scene->CreateEntity("Camera");
		auto& cam = camera.AddComponent<CameraComponent>();
		cam.Camera.SetPerspective(glm::radians(60.0f), 0.5f, 250.0f);
		camera.GetComponent<TransformComponent>().Translation = { 0.0f, 2.0f, 8.0f };

		Entity sun = scene->CreateEntity("Sun");
		auto& light = sun.AddComponent<DirectionalLightComponent>();
		light.Intensity = 4.5f;
		light.Color = { 1.0f, 0.9f, 0.8f };
		sun.GetComponent<TransformComponent>().Rotation = { -0.8f, 0.3f, 0.0f };

		Entity ground = scene->CreateEntity("Ground");
		auto& renderer = ground.AddComponent<MeshRendererComponent>();
		renderer.Mesh = "builtin://Plane";
		renderer.OverrideMaterial = true;
		renderer.MaterialOverride.BaseColor = { 0.2f, 0.6f, 0.2f, 1.0f };
		renderer.MaterialOverride.Roughness = 0.9f;
		ground.AddComponent<BoxColliderComponent>().HalfExtents = { 10.0f, 0.1f, 10.0f };
		ground.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Static;

		Entity player = scene->CreateEntity("Player");
		player.AddComponent<ScriptComponent>().Script = "Scripts/Player.lua";
		player.AddComponent<CapsuleColliderComponent>();
		player.AddComponent<AudioSourceComponent>().Clip = "Audio/step.wav";
		Entity weapon = scene->CreateEntity("Weapon");
		scene->SetParent(weapon, player, false);
		weapon.AddComponent<SphereColliderComponent>().Radius = 0.25f;
		Entity muzzle = scene->CreateEntity("Muzzle");
		scene->SetParent(muzzle, weapon, false);
		muzzle.AddComponent<PointLightComponent>().Range = 3.0f;

		Entity sky = scene->CreateEntity("Sky");
		sky.AddComponent<SkyLightComponent>().EnvironmentMap = "HDRI/studio.hdr";
		sky.AddComponent<SpotLightComponent>();
		sky.AddComponent<AudioListenerComponent>();

		Entity hud = scene->CreateEntity("HUD");
		auto& panel = hud.AddComponent<UIPanelComponent>();
		panel.Size = { 300.0f, 60.0f };
		panel.Color = { 0.0f, 0.0f, 0.0f, 0.5f };
		panel.Texture = "Textures/Frame.png";
		panel.Anchor = { 0.0f, 1.0f };
		panel.Pivot = { 0.0f, 1.0f };
		panel.SortOrder = -2;
		auto& label = hud.AddComponent<UITextComponent>();
		label.Text = "Score: 100\nLives: 3";
		label.Font = "Fonts/Title.ttf";
		label.FontSize = 40.0f;
		label.Color = { 1.0f, 0.8f, 0.2f, 1.0f };
		label.Alignment = TextAlignment::Right;
		label.LineSpacing = 1.25f;
		label.OutlineWidth = 2.0f;
		label.OutlineColor = { 0.1f, 0.0f, 0.0f, 0.75f };
		label.Anchor = { 1.0f, 0.0f };
		label.Offset = { -20.0f, 20.0f };
		label.Pivot = { 1.0f, 0.0f };
		label.SortOrder = 3;
		label.Visible = false;
		return scene;
	}

}

TEST_SUITE("Serialization")
{
	TEST_CASE("SceneSerializer: round trip preserves entities, components, hierarchy and order")
	{
		Ref<Scene> original = BuildSampleScene();
		const std::string text = SceneSerializer(original).SerializeToString();
		CHECK(text.find("Scene: Sample") != std::string::npos);

		Ref<Scene> loaded = CreateRef<Scene>();
		std::string error;
		REQUIRE_MESSAGE(SceneSerializer(loaded).DeserializeFromString(text, &error), error);
		CHECK(loaded->GetName() == "Sample");
		CHECK(loaded->GetEntityCount() == original->GetEntityCount());

		// Every entity matches component-for-component.
		auto originalEntities = original->GetAllEntitiesOrdered();
		auto loadedEntities = loaded->GetAllEntitiesOrdered();
		REQUIRE(originalEntities.size() == loadedEntities.size());
		for (size_t i = 0; i < originalEntities.size(); i++)
		{
			CAPTURE(originalEntities[i].GetName());
			CHECK(originalEntities[i].GetUUID() == loadedEntities[i].GetUUID());
			CHECK(YAML::Dump(ComponentRegistry::SerializeEntityComponents(originalEntities[i])) ==
				YAML::Dump(ComponentRegistry::SerializeEntityComponents(loadedEntities[i])));
			CHECK(originalEntities[i].GetComponent<RelationshipComponent>().Children ==
				loadedEntities[i].GetComponent<RelationshipComponent>().Children);
			CHECK(originalEntities[i].GetComponent<RelationshipComponent>().Parent ==
				loadedEntities[i].GetComponent<RelationshipComponent>().Parent);
		}

		// Second serialization is byte-identical (stable output, good for version control).
		CHECK(SceneSerializer(loaded).SerializeToString() == text);
	}

	TEST_CASE("SceneSerializer: file round trip")
	{
		const std::filesystem::path path = std::filesystem::temp_directory_path() / "ember_tests" / "scene.escene";
		Ref<Scene> original = BuildSampleScene();
		REQUIRE(SceneSerializer(original).Serialize(path));
		Ref<Scene> loaded = CreateRef<Scene>();
		REQUIRE(SceneSerializer(loaded).Deserialize(path));
		CHECK(loaded->GetEntityCount() == original->GetEntityCount());
		CHECK_FALSE(SceneSerializer(loaded).Deserialize(path.parent_path() / "missing.escene"));
		std::filesystem::remove_all(path.parent_path());
	}

	TEST_CASE("SceneSerializer: malformed input fails cleanly and leaves the scene untouched")
	{
		Ref<Scene> scene = BuildSampleScene();
		const size_t before = scene->GetEntityCount();
		SceneSerializer serializer(scene);
		std::string error;

		CHECK_FALSE(serializer.DeserializeFromString("{{{ not yaml", &error));
		CHECK_FALSE(error.empty());
		CHECK_FALSE(serializer.DeserializeFromString("Foo: bar", &error));
		CHECK(error.find("not a scene") != std::string::npos);
		CHECK_FALSE(serializer.DeserializeFromString("Scene: X\nVersion: 999\n", &error));
		CHECK(error.find("newer") != std::string::npos);
		CHECK_FALSE(serializer.DeserializeFromString("Scene: X\nEntities:\n  - Entity: 5\n  - Entity: 5\n", &error));
		CHECK(error.find("duplicate") != std::string::npos);
		CHECK_FALSE(serializer.DeserializeFromString("Scene: X\nEntities:\n  - Components: {}\n", &error));
		CHECK(scene->GetEntityCount() == before);
	}

	TEST_CASE("SceneSerializer: tolerates unknown components, missing parents and cycles")
	{
		const std::string text = R"(Scene: Robust
Version: 1
Entities:
  - Entity: 1
    Parent: 2
    Components:
      Tag: {Tag: A}
      FutureComponent: {Value: 1}
  - Entity: 2
    Parent: 1
    Components:
      Tag: {Tag: B}
  - Entity: 3
    Parent: 999
    Components:
      Tag: {Tag: C}
      Transform: {Translation: [1, 2, 3]}
)";
		Ref<Scene> scene = CreateRef<Scene>();
		REQUIRE(SceneSerializer(scene).DeserializeFromString(text));
		CHECK(scene->GetEntityCount() == 3);
		Entity a = scene->GetEntityByUUID(1);
		Entity b = scene->GetEntityByUUID(2);
		Entity c = scene->GetEntityByUUID(3);
		REQUIRE(a);
		REQUIRE(b);
		REQUIRE(c);
		CHECK_FALSE(scene->IsDescendantOf(a, a));
		CHECK_FALSE(scene->IsDescendantOf(b, b));
		CHECK_FALSE(scene->GetParent(c));
		CHECK(c.GetComponent<TransformComponent>().Translation == glm::vec3(1.0f, 2.0f, 3.0f));
	}

	TEST_CASE("Prefab: serialize subtree and instantiate with fresh UUIDs")
	{
		Ref<Scene> scene = BuildSampleScene();
		Entity player = scene->FindEntityByName("Player");
		REQUIRE(player);
		const std::string prefab = SceneSerializer::SerializePrefabToString(player);
		CHECK(prefab.find("Prefab: Player") != std::string::npos);

		const size_t before = scene->GetEntityCount();
		Entity instance = SceneSerializer::InstantiatePrefabFromString(*scene, prefab, "Prefabs/Player.eprefab");
		REQUIRE(instance);
		CHECK(scene->GetEntityCount() == before + 3);
		CHECK(instance.GetUUID() != player.GetUUID());
		CHECK(instance.GetName() == "Player");
		CHECK_FALSE(scene->GetParent(instance));
		CHECK(instance.GetComponent<PrefabComponent>().Prefab == "Prefabs/Player.eprefab");
		CHECK(instance.GetComponent<ScriptComponent>().Script == "Scripts/Player.lua");

		auto children = scene->GetChildren(instance);
		REQUIRE(children.size() == 1);
		CHECK(children[0].GetName() == "Weapon");
		auto grandchildren = scene->GetChildren(children[0]);
		REQUIRE(grandchildren.size() == 1);
		CHECK(grandchildren[0].GetName() == "Muzzle");
		CHECK(grandchildren[0].HasComponent<PointLightComponent>());

		// Instantiating twice gives two independent copies.
		Entity second = SceneSerializer::InstantiatePrefabFromString(*scene, prefab);
		REQUIRE(second);
		CHECK(second.GetUUID() != instance.GetUUID());
		CHECK_FALSE(second.HasComponent<PrefabComponent>());

		std::string error;
		CHECK_FALSE(SceneSerializer::InstantiatePrefabFromString(*scene, "Scene: notaprefab", {}, &error));
		CHECK_FALSE(error.empty());
		CHECK_FALSE(SceneSerializer::InstantiatePrefabFromString(*scene, "Prefab: Empty\nEntities: []\n", {}, &error));
	}
}
