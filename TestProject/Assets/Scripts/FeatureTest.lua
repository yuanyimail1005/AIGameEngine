-- Feature test controller: exercises every component and the entire scripting API.
-- Run automatically with:  EmberEditor --project TestProject --run-tests
-- Every Test.Check must pass, every Test.Expect name must be reached, and no script errors may occur.

local U = require("Scripts.Lib.TestUtils")

local FeatureTest = {}

local function Check(condition, name, message)
	Test.Check(condition and true or false, name, message)
end

-- Expected (component, field, value) round trips through the generic component API.
local function TestComponentRoundTrip(entity, component, values, field, expected)
	Check(entity:AddComponent(component, values), component .. ": AddComponent")
	Check(entity:HasComponent(component), component .. ": HasComponent")
	local data = entity:GetComponent(component)
	Check(data ~= nil, component .. ": GetComponent")
	if data ~= nil and field ~= nil then
		local value = data[field]
		local ok
		if type(expected) == "number" then
			ok = type(value) == "number" and U.Near(value, expected)
		else
			ok = value == expected
		end
		Check(ok, component .. "." .. field .. " round trip", tostring(value) .. " ~= " .. tostring(expected))
	end
end

function FeatureTest:OnCreate()
	self.frame = 0
	for _, name in ipairs({ "api", "components", "physics", "triggers", "projectiles", "late update", "kinematic", "complete" }) do
		Test.Expect(name)
	end

	-- Logging and printing
	Log.Info("Feature test starting")
	print("print() is routed to the log")

	-- Math types and helpers
	local a, b = vec3(1, 2, 3), vec3(4, 5, 6)
	Check(a + b == vec3(5, 7, 9), "vec3 +")
	Check(b - a == vec3(3, 3, 3), "vec3 -")
	Check(a * 2 == vec3(2, 4, 6) and 2 * a == vec3(2, 4, 6) and a * vec3(2, 2, 2) == vec3(2, 4, 6), "vec3 *")
	Check(vec3(2, 4, 6) / 2 == a, "vec3 /")
	Check(-a == vec3(-1, -2, -3), "vec3 unary -")
	Check(U.Near(a:dot(b), 32), "vec3 dot")
	Check(a:cross(b) == vec3(-3, 6, -3), "vec3 cross")
	Check(U.Near(vec3(0, 3, 4):length(), 5), "vec3 length")
	Check(U.Near(vec3(0, 3, 4):normalized():length(), 1), "vec3 normalized")
	Check(U.Near(a:distance(b), math.sqrt(27)), "vec3 distance")
	Check(U.NearVec(a:lerp(b, 0.5), vec3(2.5, 3.5, 4.5)), "vec3 lerp")
	Check(U.Near(vec2(3, 4):length(), 5) and vec2(1, 2) + vec2(1, 1) == vec2(2, 3), "vec2")
	Check(U.Near(Math.Lerp(0, 10, 0.25), 2.5) and Math.Clamp(5, 0, 1) == 1, "Math.Lerp / Clamp")
	Check(U.Near(Math.Degrees(Math.Radians(90)), 90), "Math.Radians / Degrees")
	Math.Seed(42)
	local r1 = Math.Random()
	Math.Seed(42)
	Check(Math.Random() == r1, "Math.Seed determinism")
	local ri = Math.RandomInt(3, 5)
	Check(ri >= 3 and ri <= 5 and math.type(ri) == "integer", "Math.RandomInt")
	Check(type(os.clock()) == "number" and type(os.time()) == "number", "os.clock / os.time")

	-- Input (no input is injected during the automated run)
	Check(not Input.IsKeyDown("W") and not Input.IsKeyPressed("Space") and not Input.IsKeyReleased("A"), "Input keys idle")
	Check(not Input.IsMouseButtonDown("Left") and not Input.IsMouseButtonPressed("Right") and not Input.IsMouseButtonReleased("Middle"), "Input mouse idle")
	Check(Input.GetMousePosition() ~= nil and Input.GetMouseDelta() ~= nil and Input.GetScrollDelta() ~= nil, "Input mouse vectors")

	-- Scene queries
	local camera = Scene.GetPrimaryCamera()
	Check(camera ~= nil and camera.name == "Main Camera", "Scene.GetPrimaryCamera")
	for _, name in ipairs({ "Ground", "Ball", "Faller", "TriggerZone", "Crate", "GoldStatue", "Lamp", "LampBulb", "Spotlight",
		"Spinner", "Mover", "Pusher", "Speaker", "Sun", "Sky" }) do
		Check(Scene.FindEntityByName(name) ~= nil, "scene contains " .. name)
	end
	Check(Scene.GetEntityByID(camera.id) == camera, "Scene.GetEntityByID")
	Check(Scene.GetEntityCount() == #Scene.GetEntities(), "Scene.GetEntityCount")
	Check(#Scene.GetEntitiesWith("PointLight") >= 1 and #Scene.GetEntitiesWith("Script") >= 5, "Scene.GetEntitiesWith")
	Check(U.Near(Scene.GetTime(), 0), "Scene.GetTime at start")

	-- Entity transform API on a scratch entity
	local e = Scene.CreateEntity("Scratch")
	Check(e:IsValid() and e.name == "Scratch", "Scene.CreateEntity")
	e.name = "ScratchEntity"
	Check(e.name == "ScratchEntity", "entity rename")
	e:SetPosition(vec3(10, 0, 0))
	e:Translate(vec3(0, 1, 0))
	Check(e:GetPosition() == vec3(10, 1, 0), "SetPosition / Translate")
	e:SetRotationDegrees(vec3(0, 90, 0))
	Check(U.Near(e:GetRotation().y, math.pi / 2) and U.Near(e:GetRotationDegrees().y, 90), "rotation (radians / degrees)")
	Check(U.NearVec(e:GetForward(), vec3(-1, 0, 0), 1e-3) and U.NearVec(e:GetRight(), vec3(0, 0, -1), 1e-3) and U.NearVec(e:GetUp(), vec3(0, 1, 0), 1e-3), "basis vectors")
	e:SetRotation(vec3(0, 0, 0))
	e:Rotate(vec3(0, 0.5, 0))
	Check(U.Near(e:GetRotation().y, 0.5), "Rotate")
	e:SetScale(vec3(2, 2, 2))
	Check(e:GetScale() == vec3(2, 2, 2), "SetScale")
	e:LookAt(e:GetWorldPosition() + vec3(0, 0, -3))
	Check(U.NearVec(e:GetForward(), vec3(0, 0, -1), 1e-3), "LookAt")
	e:SetWorldPosition(vec3(10, 2, 0))
	Check(U.NearVec(e:GetWorldPosition(), vec3(10, 2, 0), 1e-4), "SetWorldPosition")

	-- Hierarchy
	local child = Scene.CreateEntity("ScratchChild")
	Check(child:SetParent(e) and child:GetParent() == e and #e:GetChildren() == 1, "SetParent / GetParent / GetChildren")
	child:SetPosition(vec3(1, 0, 0))
	Check(U.NearVec(child:GetWorldPosition(), vec3(12, 2, 0), 1e-3), "child world position (parent scale 2)")
	Check(child:SetParent(nil) and child:GetParent() == nil, "unparent")
	child:Destroy()

	-- Every component through the generic API
	TestComponentRoundTrip(e, "MeshRenderer", { Mesh = "builtin://Sphere", MaterialOverride = { BaseColor = { 0.1, 0.2, 0.3, 1.0 } } }, "Mesh", "builtin://Sphere")
	TestComponentRoundTrip(e, "Camera", { Primary = false, PerspectiveFOVDegrees = 60 }, "Primary", false)
	TestComponentRoundTrip(e, "DirectionalLight", { Intensity = 0.0 }, "Intensity", 0.0)
	TestComponentRoundTrip(e, "PointLight", { Intensity = 2.0, Range = 3.0 }, "Range", 3.0)
	TestComponentRoundTrip(e, "SpotLight", { OuterConeAngle = 0.5 }, "OuterConeAngle", 0.5)
	TestComponentRoundTrip(e, "SkyLight", { Intensity = 0.5 }, "Intensity", 0.5)
	TestComponentRoundTrip(e, "RigidBody", { Type = "Kinematic", Mass = 3.0 }, "Type", "Kinematic")
	TestComponentRoundTrip(e, "BoxCollider", { HalfExtents = vec3(0.25, 0.5, 0.75) }, "HalfExtents", vec3(0.25, 0.5, 0.75))
	TestComponentRoundTrip(e, "SphereCollider", { Radius = 0.75 }, "Radius", 0.75)
	TestComponentRoundTrip(e, "CapsuleCollider", { HalfHeight = 1.25 }, "HalfHeight", 1.25)
	TestComponentRoundTrip(e, "AudioSource", { Clip = "Audio/bounce.wav", Volume = 0.25 }, "Volume", 0.25)
	TestComponentRoundTrip(e, "AudioListener", { Active = false }, "Active", false)
	TestComponentRoundTrip(e, "Prefab", { Prefab = "Prefabs/Projectile.eprefab" }, "Prefab", "Prefabs/Projectile.eprefab")
	TestComponentRoundTrip(e, "Transform", { Translation = vec3(10, 2, 0) }, "Translation", vec3(10, 2, 0))
	TestComponentRoundTrip(e, "Tag", { Tag = "ScratchEntity" }, "Tag", "ScratchEntity")
	Check(e:SetComponent("PointLight", { Range = 4.0 }) and U.Near(e:GetComponent("PointLight").Range, 4.0) and U.Near(e:GetComponent("PointLight").Intensity, 2.0), "SetComponent patch semantics")
	e:RefreshPhysics()
	for _, component in ipairs({ "Camera", "DirectionalLight", "SpotLight", "SkyLight", "SphereCollider", "CapsuleCollider", "AudioSource", "AudioListener" }) do
		Check(e:RemoveComponent(component) and not e:HasComponent(component), component .. ": RemoveComponent")
	end
	e:SetColor(1, 0, 0)
	Check(e:GetComponent("MeshRenderer").MaterialOverride.BaseColor[1] == 1, "SetColor")
	e:SetVisible(false)
	Check(e:GetComponent("MeshRenderer").Visible == false, "SetVisible")
	-- Script component added at runtime gets an instance on the next frame.
	Check(e:AddComponent("Script", { Script = "Scripts/Spinner.lua" }), "add Script at runtime")
	self.scratch = e

	-- Audio
	local speaker = Scene.FindEntityByName("Speaker")
	Check(speaker:IsSoundPlaying(), "AudioSource PlayOnStart")
	speaker:StopSound()
	Check(not speaker:IsSoundPlaying(), "StopSound")
	Check(speaker:PlaySound() and speaker:IsSoundPlaying(), "PlaySound")
	Check(Audio.PlayOneShot("Audio/bounce.wav", 0.3), "Audio.PlayOneShot")
	Audio.SetMasterVolume(0.8)
	Check(U.Near(Audio.GetMasterVolume(), 0.8), "master volume")
	Audio.SetMasterVolume(1.0)

	-- Prefabs and cross-script calls
	local projectile = Scene.Instantiate("Prefabs/Projectile.eprefab", vec3(0, 2, 5))
	Check(projectile ~= nil and projectile:GetPosition() == vec3(0, 2, 5), "Scene.Instantiate")
	Check(projectile:GetScript():GetAge() == 0, "GetScript on spawned prefab")
	Check(ProjectileStats.created == 1, "prefab script OnCreate ran")

	Test.Pass("api")
	Test.Pass("components")
end

function FeatureTest:OnUpdate(dt)
	self.frame = self.frame + 1
	local frame = self.frame

	if frame == 2 then
		local spinner = Scene.FindEntityByName("Spinner"):GetScript()
		Check(spinner.updates >= 1 and spinner.lateUpdates >= 1, "Spinner callbacks running")
		Check(self.scratch:GetScript() ~= nil, "runtime-added script instantiated")
	elseif frame == 40 then
		Check(ProjectileStats.destroyed == 1, "projectile destroyed itself (OnDestroy)")
		Check(Scene.FindEntityByName("Projectile") == nil, "projectile removed from scene")
		Test.Pass("projectiles")
	elseif frame == 150 then
		-- Physics: collisions, raycasts, forces
		Check(BallStats.collisions >= 1, "ball collision callbacks")
		Check(BallStats.lastOther == "Ground", "ball hit the ground", tostring(BallStats.lastOther))
		local hit = Scene.Raycast(vec3(-8, 10, 8), vec3(0, -1, 0), 50)
		Check(hit ~= nil and hit.entity.name == "Ground" and U.Near(hit.point.y, 0, 0.01) and U.Near(hit.normal.y, 1, 0.01), "Scene.Raycast")
		Check(Scene.Raycast(vec3(0, 50, 0), vec3(0, 1, 0), 10) == nil, "Scene.Raycast miss")

		local pusher = Scene.FindEntityByName("Pusher")
		pusher:SetVelocity(vec3(0, 0, 0))
		pusher:AddImpulse(vec3(2, 0, 0))
		Check(U.Near(pusher:GetVelocity().x, 2.0, 0.01), "AddImpulse / GetVelocity (mass 1)")
		pusher:SetVelocity(vec3(0, 0, 1))
		Check(U.Near(pusher:GetVelocity().z, 1.0, 0.01), "SetVelocity")
		pusher:SetAngularVelocity(vec3(0, 1, 0))
		Check(U.Near(pusher:GetAngularVelocity().y, 1.0, 0.01), "SetAngularVelocity")
		pusher:AddForce(vec3(0, 0, 1))
		pusher:AddTorque(vec3(0, 0.1, 0))

		local gravity = Scene.GetGravity()
		Check(U.Near(gravity.y, -9.81, 0.01), "Scene.GetGravity")
		Scene.SetGravity(vec3(0, -5, 0))
		Check(U.Near(Scene.GetGravity().y, -5, 0.01), "Scene.SetGravity")
		Scene.SetGravity(gravity)
		Test.Pass("physics")
	elseif frame == 200 then
		local mover = Scene.FindEntityByName("Mover")
		Check(not U.Near(mover:GetPosition().x, -6.0, 0.05), "kinematic mover moved by script")
		Test.Pass("kinematic")
	elseif frame == 260 then
		Check(TriggerStats.began >= 1 and TriggerStats.lastOther == "Faller", "trigger begin", tostring(TriggerStats.lastOther))
		Check(TriggerStats.ended >= 1, "trigger end")
		Test.Pass("triggers")
		Check(Scene.GetTime() > 4.0, "Scene.GetTime advances")
		Check(self.scratch:IsValid(), "scratch entity alive")
		Scene.Destroy(self.scratch)
		Test.Pass("complete")
		Log.Info("Feature test complete")
	end
end

function FeatureTest:OnLateUpdate(dt)
	-- Late updates run after every script's OnUpdate; the Spinner (created earlier) has finished both by now.
	if self.frame == 200 then
		local spinner = Scene.FindEntityByName("Spinner"):GetScript()
		Check(spinner.lateUpdates == spinner.updates and spinner.updates >= 200, "OnLateUpdate follows OnUpdate",
			spinner.updates .. " updates, " .. spinner.lateUpdates .. " late updates")
		Test.Pass("late update")
	end
end

return FeatureTest
