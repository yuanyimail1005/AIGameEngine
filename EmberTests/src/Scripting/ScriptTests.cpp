#include <doctest/doctest.h>

#include "TestUtils.h"

#include "Ember/Core/Input.h"
#include "Ember/Scene/ComponentRegistry.h"
#include "Ember/Scene/Entity.h"
#include "Ember/Scene/Scene.h"
#include "Ember/Scene/SceneSerializer.h"
#include "Ember/Scripting/ScriptEngine.h"

#include <cstdlib>

using namespace Ember;

namespace {

	struct ScriptFixture
	{
		std::filesystem::path Assets;
		Ref<Scene> SceneRef;

		explicit ScriptFixture(const std::string& name)
			: Assets(EmberTest::MakeTempDirectory(name)), SceneRef(CreateRef<Scene>("ScriptTest"))
		{
			SceneRef->SetAssetDirectory(Assets);
		}

		Entity AddScripted(const std::string& entityName, const std::string& scriptPath, const std::string& source)
		{
			EmberTest::WriteFile(Assets / scriptPath, source);
			Entity entity = SceneRef->CreateEntity(entityName);
			entity.AddComponent<ScriptComponent>().Script = scriptPath;
			return entity;
		}

		void Run(int frames)
		{
			for (int i = 0; i < frames; i++)
			{
				SceneRef->OnUpdateRuntime(Timestep(1.0f / 60.0f));
				Input::EndFrame();
			}
		}

		ScriptEngine& Scripts() { return *SceneRef->GetScriptEngine(); }

		void RequireAllTestsPassed()
		{
			for (const ScriptTestResult& result : Scripts().GetTestResults())
			{
				CAPTURE(result.Name);
				CAPTURE(result.Message);
				CHECK(result.Passed);
			}
			for (const std::string& error : Scripts().GetErrors())
				FAIL_CHECK(error);
		}
	};

}

TEST_SUITE("Scripting")
{
	TEST_CASE("Scripting: lifecycle callbacks run in order with dt")
	{
		ScriptFixture fixture("script_lifecycle");
		fixture.AddScripted("Counter", "Scripts/Counter.lua", R"(
local Counter = {}
function Counter:OnCreate()
	self.updates = 0
	self.late = 0
	self.total = 0
	Log.Info("created", self.entity.name)
end
function Counter:OnUpdate(dt)
	self.updates = self.updates + 1
	self.total = self.total + dt
end
function Counter:OnLateUpdate(dt)
	self.late = self.late + 1
	Test.Check(self.late == self.updates, "late follows update")
end
return Counter
)");
		fixture.SceneRef->OnRuntimeStart();
		fixture.Run(30);

		std::string result;
		REQUIRE(fixture.Scripts().ExecuteString("Scene.FindEntityByName('Counter'):GetScript().updates", &result));
		CHECK(result == "30");
		REQUIRE(fixture.Scripts().ExecuteString("string.format('%.3f', Scene.FindEntityByName('Counter'):GetScript().total)", &result));
		CHECK(result == "0.500");
		fixture.RequireAllTestsPassed();
		fixture.SceneRef->OnRuntimeStop();
	}

	TEST_CASE("Scripting: entity, transform, hierarchy and component API")
	{
		ScriptFixture fixture("script_entity_api");
		fixture.AddScripted("Tester", "Scripts/Tester.lua", R"(
local T = {}
function T:OnCreate()
	local e = self.entity
	Test.Check(e:IsValid(), "self is valid")
	Test.Check(e.name == "Tester", "name")
	e.name = "Renamed"
	Test.Check(e.name == "Renamed", "rename")

	-- math types
	local a = vec3(1, 2, 3)
	local b = vec3(4, 5, 6)
	Test.Check((a + b) == vec3(5, 7, 9), "vec3 add")
	Test.Check((b - a) == vec3(3, 3, 3), "vec3 sub")
	Test.Check((a * 2) == vec3(2, 4, 6) and (2 * a) == vec3(2, 4, 6), "vec3 scale")
	Test.Check(math.abs(a:dot(b) - 32) < 1e-5, "vec3 dot")
	Test.Check(a:cross(b) == vec3(-3, 6, -3), "vec3 cross")
	Test.Check(math.abs(vec3(3, 4, 0):length() - 5) < 1e-5, "vec3 length")
	Test.Check(math.abs(vec2(3, 4):length() - 5) < 1e-5, "vec2 length")
	Test.Check(tostring(vec3(1, 2, 3)):find("vec3") ~= nil, "vec3 tostring")
	Test.Check(Math.Clamp(5, 0, 1) == 1 and math.abs(Math.Lerp(0, 10, 0.5) - 5) < 1e-6, "Math helpers")
	local r = Math.Random(2, 3)
	Test.Check(r >= 2 and r <= 3, "Math.Random range")

	-- transform
	e:SetPosition(vec3(1, 2, 3))
	Test.Check(e:GetPosition() == vec3(1, 2, 3), "position")
	e:Translate(vec3(1, 0, 0))
	Test.Check(e:GetPosition() == vec3(2, 2, 3), "translate")
	e:SetRotationDegrees(vec3(0, 90, 0))
	Test.Check(math.abs(e:GetRotation().y - math.pi / 2) < 1e-4, "rotation degrees")
	local f = e:GetForward()
	Test.Check(math.abs(f.x + 1) < 1e-4, "forward after 90 deg yaw is -X")
	e:SetScale(vec3(2, 2, 2))
	Test.Check(e:GetScale() == vec3(2, 2, 2), "scale")
	e:SetRotation(vec3(0, 0, 0))
	e:LookAt(e:GetWorldPosition() + vec3(0, 0, -5))
	Test.Check(e:GetForward():distance(vec3(0, 0, -1)) < 1e-3, "LookAt")

	-- hierarchy
	local child = Scene.CreateEntity("Child")
	Test.Check(child:SetParent(e), "SetParent")
	Test.Check(child:GetParent() == e, "GetParent")
	Test.Check(#e:GetChildren() == 1, "GetChildren")
	child:SetPosition(vec3(1, 0, 0))
	local wp = child:GetWorldPosition()
	Test.Check(wp:distance(vec3(4, 2, 3)) < 1e-3, "world position respects parent scale")
	child:SetWorldPosition(vec3(0, 0, 0))
	Test.Check(child:GetWorldPosition():distance(vec3(0, 0, 0)) < 1e-3, "SetWorldPosition")
	Test.Check(child:SetParent(nil) and child:GetParent() == nil, "unparent")

	-- generic components
	Test.Check(not e:HasComponent("PointLight"), "no light yet")
	Test.Check(e:AddComponent("PointLight", { Intensity = 5, Color = vec3(1, 0, 0) }), "AddComponent")
	local light = e:GetComponent("PointLight")
	Test.Check(light.Intensity == 5, "GetComponent value")
	Test.Check(light.Color == vec3(1, 0, 0), "GetComponent vec3")
	Test.Check(e:SetComponent("PointLight", { Range = 20 }), "SetComponent patch")
	Test.Check(e:GetComponent("PointLight").Range == 20 and e:GetComponent("PointLight").Intensity == 5, "patch keeps fields")
	Test.Check(e:RemoveComponent("PointLight") and not e:HasComponent("PointLight"), "RemoveComponent")
	Test.Check(e:GetComponent("Transform").Scale == vec3(2, 2, 2), "Transform via generic API")
	e:SetColor(0, 1, 0)
	local mr = e:GetComponent("MeshRenderer")
	Test.Check(mr.OverrideMaterial == true and mr.MaterialOverride.BaseColor[2] == 1, "SetColor")

	-- scene queries
	Test.Check(Scene.FindEntityByName("Child") == child, "FindEntityByName")
	Test.Check(Scene.GetEntityByID(child.id) == child, "GetEntityByID")
	Test.Check(Scene.FindEntityByName("Nope") == nil, "missing entity is nil")
	Test.Check(#Scene.GetEntitiesWith("Script") == 1, "GetEntitiesWith")
	Test.Check(Scene.GetEntityCount() == #Scene.GetEntities(), "entity count")
	child:Destroy()
	Test.Pass("api complete")
end
return T
)");
		fixture.SceneRef->OnRuntimeStart();
		fixture.Run(2);
		fixture.RequireAllTestsPassed();
		CHECK(fixture.Scripts().GetTestResults().size() > 30);
		CHECK_FALSE(fixture.SceneRef->FindEntityByName("Child")); // destroyed at end of frame
		CHECK(fixture.SceneRef->FindEntityByName("Renamed"));
		fixture.SceneRef->OnRuntimeStop();
	}

	TEST_CASE("Scripting: UI text and panel components drive a HUD")
	{
		ScriptFixture fixture("script_ui");
		Entity hud = fixture.AddScripted("HUD", "Scripts/HUD.lua", R"(
local HUD = {}
function HUD:OnCreate()
	self.score = 0
	local e = self.entity
	Test.Check(e:AddComponent("UIText", { Text = "Score: 0", FontSize = 48, Anchor = vec2(0, 0), Pivot = vec2(0, 0),
		Offset = vec2(20, 20), Color = { 1, 0.8, 0.2, 1 }, Alignment = "Right" }), "add UIText")
	local bar = Scene.CreateEntity("HealthBar")
	Test.Check(bar:AddComponent("UIPanel", { Size = vec2(200, 20), Color = { 0, 1, 0, 0.8 }, SortOrder = -1 }), "add UIPanel")
	local ui = e:GetComponent("UIText")
	Test.Check(ui.Text == "Score: 0" and ui.FontSize == 48, "UIText values")
	Test.Check(ui.Anchor == vec2(0, 0) and ui.Offset == vec2(20, 20), "UIText vec2 fields")
	Test.Check(ui.Alignment == "Right", "UIText alignment")
	Test.Check(math.abs(ui.Color[2] - 0.8) < 1e-6 and ui.Color[4] == 1, "UIText color array")
	Test.Check(bar:GetComponent("UIPanel").SortOrder == -1, "UIPanel sort order")
	-- String fields stay strings even when they look like numbers or booleans.
	e:SetComponent("UIText", { Text = "100" })
	Test.Check(e:GetComponent("UIText").Text == "100", "numeric text stays a string")
	e:SetComponent("UIText", { Text = "true" })
	Test.Check(e:GetComponent("UIText").Text == "true", "boolean-like text stays a string")
	Test.Check(type(e:GetComponent("UIText").FontSize) == "number", "numbers stay numbers")
end
function HUD:OnUpdate(dt)
	self.score = self.score + 10
	self.entity:SetComponent("UIText", { Text = "Score: " .. self.score })
	local bar = Scene.FindEntityByName("HealthBar")
	bar:SetComponent("UIPanel", { Size = vec2(200 - self.score, 20) })
end
return HUD
)");
		fixture.SceneRef->OnRuntimeStart();
		fixture.Run(3);
		fixture.RequireAllTestsPassed();

		REQUIRE(hud.HasComponent<UITextComponent>());
		const auto& text = hud.GetComponent<UITextComponent>();
		CHECK(text.Text == "Score: 30");
		CHECK(text.FontSize == doctest::Approx(48.0f));
		CHECK(text.Alignment == TextAlignment::Right);
		CHECK(text.Color == glm::vec4(1.0f, 0.8f, 0.2f, 1.0f));
		CHECK(text.Offset == glm::vec2(20.0f, 20.0f));
		Entity bar = fixture.SceneRef->FindEntityByName("HealthBar");
		REQUIRE(bar);
		CHECK(bar.GetComponent<UIPanelComponent>().Size == glm::vec2(170.0f, 20.0f));
		CHECK(bar.GetComponent<UIPanelComponent>().SortOrder == -1);

		// Unknown enum values are rejected and leave the field unchanged; valid fields in the same patch still apply.
		CHECK_FALSE(ComponentRegistry::Find("UIText")->Deserialize(hud, YAML::Load("{Alignment: Diagonal, FontSize: 20}")));
		CHECK(text.Alignment == TextAlignment::Right);
		CHECK(text.FontSize == doctest::Approx(20.0f));
		CHECK(ComponentRegistry::Find("uitextcomponent") == ComponentRegistry::Find("UIText"));
		fixture.SceneRef->OnRuntimeStop();
	}

	TEST_CASE("Scripting: input queries reflect injected events")
	{
		ScriptFixture fixture("script_input");
		fixture.AddScripted("Mover", "Scripts/Mover.lua", R"(
local M = {}
function M:OnCreate() self.jumps = 0 end
function M:OnUpdate(dt)
	if Input.IsKeyDown("D") then self.entity:Translate(vec3(1, 0, 0)) end
	if Input.IsKeyPressed("Space") then self.jumps = self.jumps + 1 end
	if Input.IsMouseButtonDown("Left") then self.clicked = true end
end
return M
)");
		Input::Reset();
		fixture.SceneRef->OnRuntimeStart();
		KeyPressedEvent pressD(KeyCode::D, false);
		Input::OnEvent(pressD);
		KeyPressedEvent pressSpace(KeyCode::Space, false);
		Input::OnEvent(pressSpace);
		MouseButtonPressedEvent click(MouseButton::Left);
		Input::OnEvent(click);
		fixture.Run(3); // Space is "just pressed" only in the first frame
		KeyReleasedEvent releaseD(KeyCode::D);
		Input::OnEvent(releaseD);
		fixture.Run(2);

		Entity mover = fixture.SceneRef->FindEntityByName("Mover");
		CHECK(mover.GetComponent<TransformComponent>().Translation.x == doctest::Approx(3.0f));
		std::string result;
		REQUIRE(fixture.Scripts().ExecuteString("Scene.FindEntityByName('Mover'):GetScript().jumps", &result));
		CHECK(result == "1");
		REQUIRE(fixture.Scripts().ExecuteString("Scene.FindEntityByName('Mover'):GetScript().clicked", &result));
		CHECK(result == "true");
		Input::Reset();
		fixture.SceneRef->OnRuntimeStop();
	}

	TEST_CASE("Scripting: physics API and collision callbacks")
	{
		ScriptFixture fixture("script_physics");
		Entity ground = fixture.SceneRef->CreateEntity("Ground");
		ground.AddComponent<BoxColliderComponent>().HalfExtents = { 20.0f, 0.5f, 20.0f };
		ground.GetComponent<TransformComponent>().Translation = { 0.0f, -0.5f, 0.0f };

		Entity ball = fixture.AddScripted("Ball", "Scripts/Ball.lua", R"(
local Ball = {}
function Ball:OnCreate()
	self.hits = 0
	self.entity:AddComponent("RigidBody", { Type = "Dynamic", Mass = 2 })
	self.entity:AddComponent("SphereCollider", { Radius = 0.5 })
end
function Ball:OnCollisionBegin(other)
	self.hits = self.hits + 1
	self.lastHit = other.name
end
return Ball
)");
		ball.GetComponent<TransformComponent>().Translation = { 0.0f, 3.0f, 0.0f };
		fixture.SceneRef->OnRuntimeStart();
		fixture.Run(150);

		std::string result;
		REQUIRE(fixture.Scripts().ExecuteString("Scene.FindEntityByName('Ball'):GetScript().hits", &result));
		CHECK(result == "1");
		REQUIRE(fixture.Scripts().ExecuteString("Scene.FindEntityByName('Ball'):GetScript().lastHit", &result));
		CHECK(result == "Ground");
		CHECK(ball.GetComponent<TransformComponent>().Translation.y == doctest::Approx(0.5f).epsilon(0.05));

		REQUIRE(fixture.Scripts().ExecuteString(R"(
local b = Scene.FindEntityByName('Ball')
Scene.SetGravity(vec3(0, 0, 0))
b:SetVelocity(vec3(0, 0, 0))
b:AddImpulse(vec3(4, 0, 0))
return b:GetVelocity().x)", &result));
		CHECK(std::atof(result.c_str()) == doctest::Approx(2.0)); // impulse 4 / mass 2

		REQUIRE(fixture.Scripts().ExecuteString("Scene.Raycast(vec3(0, 10, 0), vec3(0, -1, 0)).entity.name", &result));
		CHECK(result == "Ball");
		REQUIRE(fixture.Scripts().ExecuteString("Scene.Raycast(vec3(0, 10, 0), vec3(0, 1, 0), 5) == nil", &result));
		CHECK(result == "true");
		CHECK(fixture.Scripts().GetErrorCount() == 0);
		fixture.SceneRef->OnRuntimeStop();
	}

	TEST_CASE("Scripting: spawning prefabs, destroying entities, OnDestroy and cross-script calls")
	{
		ScriptFixture fixture("script_spawn");
		// Prefab with its own script.
		{
			Ref<Scene> prefabScene = CreateRef<Scene>();
			Entity bullet = prefabScene->CreateEntity("Bullet");
			bullet.AddComponent<ScriptComponent>().Script = "Scripts/Bullet.lua";
			EmberTest::WriteFile(fixture.Assets / "Prefabs/Bullet.eprefab", SceneSerializer::SerializePrefabToString(bullet));
		}
		EmberTest::WriteFile(fixture.Assets / "Scripts/Bullet.lua", R"(
local Bullet = {}
function Bullet:OnCreate() self.age = 0 end
function Bullet:OnUpdate(dt)
	self.age = self.age + 1
	if self.age >= 3 then self.entity:Destroy() end
end
function Bullet:OnDestroy() Spawner.destroyed = (Spawner.destroyed or 0) + 1 end
function Bullet:GetAge() return self.age end
return Bullet
)");
		EmberTest::WriteFile(fixture.Assets / "Scripts/Util.lua", "return { Double = function(x) return x * 2 end }\n");
		fixture.AddScripted("Spawner", "Scripts/Spawner.lua", R"(
Spawner = {} -- global so bullets can report back
local Util = require("Scripts.Util")
function Spawner:OnCreate()
	Test.Check(Util.Double(21) == 42, "require module")
	local b = Scene.Instantiate("Prefabs/Bullet.eprefab", vec3(1, 2, 3))
	Test.Check(b ~= nil, "instantiate")
	Test.Check(b:GetPosition() == vec3(1, 2, 3), "instantiate position")
	Test.Check(b:GetScript():GetAge() == 0, "cross-script call on fresh instance")
	Test.Check(Scene.Instantiate("Prefabs/Missing.eprefab") == nil, "missing prefab returns nil")
	Scene.Instantiate("Prefabs/Bullet.eprefab")
end
return Spawner
)");
		fixture.SceneRef->OnRuntimeStart();
		CHECK(fixture.SceneRef->GetEntityCount() == 3);
		fixture.Run(10);
		CHECK(fixture.SceneRef->GetEntityCount() == 1);

		std::string result;
		REQUIRE(fixture.Scripts().ExecuteString("Spawner.destroyed", &result));
		CHECK(result == "2");
		for (const ScriptTestResult& test : fixture.Scripts().GetTestResults())
		{
			CAPTURE(test.Name);
			CHECK(test.Passed);
		}
		// The deliberate missing-prefab instantiation is the only error.
		CHECK(fixture.Scripts().GetErrorCount() == 1);
		fixture.SceneRef->OnRuntimeStop();
	}

	TEST_CASE("Scripting: errors are reported, isolate the failing script and do not stop others")
	{
		ScriptFixture fixture("script_errors");
		fixture.AddScripted("Broken", "Scripts/Broken.lua", "local x = = 1\n");
		fixture.AddScripted("NoTable", "Scripts/NoTable.lua", "return 5\n");
		fixture.AddScripted("Missing", "Scripts/Exists.lua", "return {}\n");
		fixture.SceneRef->FindEntityByName("Missing").GetComponent<ScriptComponent>().Script = "Scripts/DoesNotExist.lua";
		fixture.AddScripted("Thrower", "Scripts/Thrower.lua", R"(
local T = {}
function T:OnUpdate(dt) error("boom") end
return T
)");
		fixture.AddScripted("Healthy", "Scripts/Healthy.lua", R"(
local H = {}
function H:OnCreate() self.n = 0 end
function H:OnUpdate(dt) self.n = self.n + 1 end
return H
)");
		fixture.SceneRef->OnRuntimeStart();
		fixture.Run(5);

		const auto& errors = fixture.Scripts().GetErrors();
		CHECK(errors.size() == 4); // syntax, not a table, missing file, runtime error (reported once)
		bool sawBoom = false;
		for (const std::string& error : errors)
			sawBoom |= error.find("boom") != std::string::npos;
		CHECK(sawBoom);

		std::string result;
		REQUIRE(fixture.Scripts().ExecuteString("Scene.FindEntityByName('Healthy'):GetScript().n", &result));
		CHECK(result == "5");
		CHECK_FALSE(fixture.Scripts().HasInstance(fixture.SceneRef->FindEntityByName("Thrower")));

		std::string error;
		CHECK_FALSE(fixture.Scripts().ExecuteString("this is not lua", nullptr, &error));
		CHECK_FALSE(error.empty());
		fixture.SceneRef->OnRuntimeStop();
	}

	TEST_CASE("Scripting: sandbox blocks file and process access")
	{
		ScriptFixture fixture("script_sandbox");
		fixture.SceneRef->OnRuntimeStart();
		std::string result;
		REQUIRE(fixture.Scripts().ExecuteString("io == nil and os.execute == nil and loadfile == nil and dofile == nil and load == nil", &result));
		CHECK(result == "true");
		REQUIRE(fixture.Scripts().ExecuteString("type(os.clock()) == 'number' and type(debug.traceback) == 'function'", &result));
		CHECK(result == "true");
		std::string error;
		CHECK_FALSE(fixture.Scripts().ExecuteString("require('../../etc/passwd')", nullptr, &error));
		CHECK(error.find("invalid module name") != std::string::npos);
		CHECK_FALSE(fixture.Scripts().ExecuteString("require('/etc/passwd')", nullptr, &error));
		REQUIRE(fixture.Scripts().ExecuteString("Scene.Instantiate('/etc/passwd') == nil and Scene.Instantiate('../x.eprefab') == nil", &result));
		CHECK(result == "true");
		REQUIRE(fixture.Scripts().ExecuteString("Audio.PlayOneShot('../../secret.wav')", &result));
		CHECK(result == "false");
		fixture.SceneRef->OnRuntimeStop();
	}

	TEST_CASE("Scripting: script removed or changed at runtime triggers OnDestroy / new OnCreate")
	{
		ScriptFixture fixture("script_swap");
		Entity entity = fixture.AddScripted("Swapper", "Scripts/A.lua", R"(
local A = {}
function A:OnCreate() Events = (Events or "") .. "A+" end
function A:OnDestroy() Events = Events .. "A-" end
return A
)");
		EmberTest::WriteFile(fixture.Assets / "Scripts/B.lua", R"(
local B = {}
function B:OnCreate() Events = Events .. "B+" end
function B:OnDestroy() Events = Events .. "B-" end
return B
)");
		fixture.SceneRef->OnRuntimeStart();
		fixture.Run(1);
		entity.GetComponent<ScriptComponent>().Script = "Scripts/B.lua";
		fixture.Run(1);
		entity.RemoveComponent<ScriptComponent>();
		fixture.Run(1);
		std::string result;
		REQUIRE(fixture.Scripts().ExecuteString("Events", &result));
		CHECK(result == "A+A-B+B-");
		fixture.SceneRef->OnRuntimeStop();
	}
}
