#!/usr/bin/env python3
"""Builds "Orb Collector", a small original arcade game, entirely through Ember's AI control interface.

Roll the ball with WASD (or the arrow keys), collect all glowing orbs before time runs out, avoid the spinning
sweeper and do not fall off the platform. This script is an example of the workflow described in
.claude/skills/ember-build-game/SKILL.md: build the scene, write scripts, play-test with simulated input, export.

Usage: python3 Examples/OrbCollector/build_game.py [--export DIR]
"""

import argparse
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "Tools"))
from ember_ai import EmberEditor  # noqa: E402

PROJECT = os.path.join(HERE, "Project")

PLAYER_SCRIPT = r'''
-- Player: rolls the ball with forces relative to the camera; respawns after falling off.
local Player = {}

function Player:OnCreate()
	self.force = 18
	self.spawn = self.entity:GetPosition()
end

function Player:OnUpdate(dt)
	local input = vec3(0, 0, 0)
	if Input.IsKeyDown("W") or Input.IsKeyDown("Up") then input.z = input.z - 1 end
	if Input.IsKeyDown("S") or Input.IsKeyDown("Down") then input.z = input.z + 1 end
	if Input.IsKeyDown("A") or Input.IsKeyDown("Left") then input.x = input.x - 1 end
	if Input.IsKeyDown("D") or Input.IsKeyDown("Right") then input.x = input.x + 1 end
	if input:length() > 0 then
		self.entity:AddForce(input:normalized() * self.force)
	end
	if self.entity:GetPosition().y < -10 then
		self:Respawn()
	end
end

function Player:Respawn()
	self.entity:SetPosition(self.spawn)
	self.entity:SetVelocity(vec3(0, 0, 0))
	self.entity:SetAngularVelocity(vec3(0, 0, 0))
	Scene.FindEntityByName("GameManager"):GetScript():OnPlayerFell()
end

return Player
'''

ORB_SCRIPT = r'''
-- Orb: bobs and spins; collected when the player touches it.
local Orb = {}

function Orb:OnCreate()
	self.base = self.entity:GetPosition()
	self.phase = Math.Random(0, 6.28)
end

function Orb:OnUpdate(dt)
	self.phase = self.phase + dt * 3
	self.entity:SetPosition(self.base + vec3(0, math.sin(self.phase) * 0.15, 0))
	self.entity:Rotate(vec3(0, dt * 2, 0))
end

function Orb:OnTriggerBegin(other)
	if other.name == "Player" and not self.collected then
		self.collected = true
		Audio.PlayOneShot("Audio/pickup.wav", 0.8, self.entity:GetWorldPosition())
		Scene.FindEntityByName("GameManager"):GetScript():OnOrbCollected()
		self.entity:Destroy()
	end
end

return Orb
'''

SWEEPER_SCRIPT = r'''
-- Sweeper: a kinematic bar rotating around the platform center.
local Sweeper = {}

function Sweeper:OnCreate()
	self.speed = 0.9
end

function Sweeper:OnUpdate(dt)
	self.entity:Rotate(vec3(0, self.speed * dt, 0))
end

return Sweeper
'''

GAME_MANAGER_SCRIPT = r'''
-- GameManager: spawns orbs, tracks score and time, decides win / loss.
local Game = {}

function Game:OnCreate()
	self.total = 8
	self.collected = 0
	self.falls = 0
	self.timeLimit = 90
	self.state = "playing"
	Math.Seed(7)
	for i = 1, self.total do
		local angle = (i / self.total) * math.pi * 2
		local radius = 3 + (i % 2) * 3
		Scene.Instantiate("Prefabs/Orb.eprefab", vec3(math.cos(angle) * radius, 0.8, math.sin(angle) * radius))
	end
	Log.Info("Collect " .. self.total .. " orbs in " .. self.timeLimit .. " seconds!")
end

function Game:OnUpdate(dt)
	if self.state ~= "playing" then return end
	if Scene.GetTime() > self.timeLimit then
		self.state = "lost"
		Log.Warn("Time is up! Orbs collected: " .. self.collected .. "/" .. self.total)
	end
end

function Game:OnOrbCollected()
	self.collected = self.collected + 1
	-- Each collected orb lights up one marker on the scoreboard.
	local marker = Scene.FindEntityByName("Marker" .. self.collected)
	if marker then
		marker:SetComponent("MeshRenderer", { MaterialOverride = { EmissiveColor = vec3(0.2, 1.0, 0.4), EmissiveStrength = 3 } })
	end
	Log.Info("Orb " .. self.collected .. "/" .. self.total)
	if self.collected >= self.total then
		self.state = "won"
		Audio.PlayOneShot("Audio/win.wav", 1.0)
		Log.Info("You win! Time: " .. string.format("%.1f", Scene.GetTime()) .. "s, falls: " .. self.falls)
	end
end

function Game:OnPlayerFell()
	self.falls = self.falls + 1
	Audio.PlayOneShot("Audio/fall.wav", 0.8)
end

return Game
'''

CAMERA_SCRIPT = r'''
-- Camera: follows the player smoothly from above and behind.
local Follow = {}

function Follow:OnCreate()
	self.offset = vec3(0, 9, 11)
	self.target = Scene.FindEntityByName("Player")
end

function Follow:OnLateUpdate(dt)
	if not self.target:IsValid() then return end
	local goal = self.target:GetWorldPosition() + self.offset
	local position = self.entity:GetPosition()
	self.entity:SetPosition(position:lerp(goal, Math.Clamp(dt * 4, 0, 1)))
	self.entity:LookAt(self.target:GetWorldPosition())
end

return Follow
'''


def write_tone(path, frequencies, seconds):
    import math
    import struct
    import wave
    rate = 22050
    frames = bytearray()
    count = int(seconds * rate)
    for i in range(count):
        t = i / rate
        f = frequencies[min(int(t / seconds * len(frequencies)), len(frequencies) - 1)]
        envelope = math.exp(-t * 4.0)
        frames += struct.pack("<h", int(12000 * envelope * math.sin(2 * math.pi * f * t)))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(bytes(frames))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--export", help="export the finished game to this directory")
    parser.add_argument("--screenshot", default=os.path.join(HERE, "screenshot.png"))
    args = parser.parse_args()

    shutil.rmtree(PROJECT, ignore_errors=True)
    with EmberEditor.launch() as editor:
        editor.call("new_project", path=PROJECT, name="OrbCollector")
        assets = editor.call("get_info")["asset_directory"]
        write_tone(os.path.join(assets, "Audio", "pickup.wav"), [880, 1320], 0.25)
        write_tone(os.path.join(assets, "Audio", "win.wav"), [523, 659, 784, 1047], 1.2)
        write_tone(os.path.join(assets, "Audio", "fall.wav"), [330, 220, 110], 0.6)

        for name, source in [("Player", PLAYER_SCRIPT), ("Orb", ORB_SCRIPT), ("Sweeper", SWEEPER_SCRIPT),
                             ("GameManager", GAME_MANAGER_SCRIPT), ("FollowCamera", CAMERA_SCRIPT)]:
            editor.call("write_asset", path=f"Scripts/{name}.lua", content=source.lstrip())

        # Start from the starter scene: reuse its camera, sun and sky.
        editor.call("set_component", entity="Main Camera", component="Transform",
                    values={"Translation": [0, 9, 11], "RotationDegrees": [-40, 0, 0]})
        editor.call("add_component", entity="Main Camera", component="Script", values={"Script": "Scripts/FollowCamera.lua"})
        editor.call("set_component", entity="Sun", component="DirectionalLight", values={"Intensity": 3.0, "ShadowSoftness": 0.8})
        editor.call("set_component", entity="Sky", component="SkyLight", values={"Intensity": 0.8})

        def create(name, **components):
            return editor.call("create_entity", name=name, components=components)

        create("Platform", Transform={"Translation": [0, -0.25, 0], "Scale": [16, 0.5, 16]},
               MeshRenderer={"Mesh": "builtin://Cube", "MaterialOverride": {"BaseColor": [0.18, 0.2, 0.26], "Roughness": 0.6}},
               BoxCollider={}, RigidBody={"Type": "Static", "Friction": 0.8})
        for i, (x, z, sx, sz) in enumerate([(0, -8.25, 16.5, 0.5), (-8.25, 0, 0.5, 6), (8.25, 0, 0.5, 6)]):
            create(f"Rail{i}", Transform={"Translation": [x, 0.25, z], "Scale": [sx, 0.5, sz]},
                   MeshRenderer={"Mesh": "builtin://Cube", "MaterialOverride": {"BaseColor": [0.9, 0.45, 0.15], "Metallic": 0.5,
                                                                              "Roughness": 0.35}},
                   BoxCollider={})
        create("Player", Transform={"Translation": [0, 0.6, 6]},
               MeshRenderer={"Mesh": "builtin://Sphere", "MaterialOverride": {"BaseColor": [0.95, 0.95, 1.0], "Metallic": 0.9,
                                                                            "Roughness": 0.15}},
               RigidBody={"Type": "Dynamic", "Mass": 1.0, "LinearDamping": 0.4, "AngularDamping": 0.4, "Friction": 0.9},
               SphereCollider={}, Script={"Script": "Scripts/Player.lua"})
        sweeper = create("Sweeper", Transform={"Translation": [0, 0.4, 0]},
                         RigidBody={"Type": "Kinematic"}, Script={"Script": "Scripts/Sweeper.lua"})
        editor.call("create_entity", name="SweeperBar", parent=sweeper["id"], components={
            "Transform": {"Scale": [11, 0.4, 0.4]},
            "MeshRenderer": {"Mesh": "builtin://Cube", "MaterialOverride": {"BaseColor": [0.8, 0.1, 0.15], "EmissiveColor": [0.6, 0.05, 0.05]}}})
        editor.call("add_component", entity="Sweeper", component="BoxCollider", values={"HalfExtents": [5.5, 0.2, 0.2]})
        create("CenterLight", Transform={"Translation": [0, 3, 0]}, PointLight={"Color": [1.0, 0.5, 0.3], "Intensity": 8, "Range": 9})
        for i in range(1, 9):
            create(f"Marker{i}", Transform={"Translation": [-3.5 + i * 0.8 - 0.4, 0.3, -9.2], "Scale": [0.5, 0.5, 0.5]},
                   MeshRenderer={"Mesh": "builtin://Sphere", "MaterialOverride": {"BaseColor": [0.1, 0.1, 0.12]}})

        # Orb prefab (a trigger with a glowing sphere)
        orb = create("Orb", Transform={"Scale": [0.6, 0.6, 0.6]},
                     MeshRenderer={"Mesh": "builtin://Sphere", "CastShadows": False,
                                   "MaterialOverride": {"BaseColor": [0.1, 0.4, 0.2], "EmissiveColor": [0.3, 1.0, 0.5], "EmissiveStrength": 2.5}},
                     RigidBody={"Type": "Kinematic", "IsTrigger": True}, SphereCollider={}, Script={"Script": "Scripts/Orb.lua"})
        editor.call("create_prefab", entity=orb["id"], path="Prefabs/Orb.eprefab")
        editor.call("destroy_entity", entity=orb["id"])

        create("GameManager", Script={"Script": "Scripts/GameManager.lua"})
        editor.call("save_scene")

        # --- Play-test with simulated input -------------------------------------------------------
        editor.call("run_frames", frames=30)
        game = "Scene.FindEntityByName('GameManager'):GetScript()"
        assert editor.call("execute_lua", code=f"#Scene.GetEntitiesWith('Script')")["result"] != "0"
        start_z = float(editor.call("execute_lua", code="Scene.FindEntityByName('Player'):GetPosition().z")["result"])
        editor.call("send_input", key="W", action="press")
        editor.call("run_frames", frames=60)
        editor.call("send_input", key="W", action="release")
        moved_z = float(editor.call("execute_lua", code="Scene.FindEntityByName('Player'):GetPosition().z")["result"])
        assert moved_z < start_z - 0.5, f"W should roll the player forward (z {start_z:.2f} -> {moved_z:.2f})"

        # Collect two more orbs by moving the player onto them (exercises triggers, scoring, prefab destruction).
        # (Rolling forward above may already have collected the orb straight ahead.)
        before = int(editor.call("execute_lua", code=f"{game}.collected")["result"])
        for _ in range(2):
            editor.call("execute_lua", code="local orb = Scene.FindEntityByName('Orb'); local p = Scene.FindEntityByName('Player'); "
                                            "p:SetPosition(orb:GetPosition()); p:SetVelocity(vec3(0,0,0))")
            editor.call("run_frames", frames=10)
        collected = int(editor.call("execute_lua", code=f"{game}.collected")["result"])
        assert collected == before + 2, f"expected {before + 2} orbs collected, got {collected}"
        lit = editor.call("get_component", entity=f"Marker{collected}", component="MeshRenderer")
        assert lit["MaterialOverride"]["EmissiveStrength"] == 3, "scoreboard marker should light up"

        # Falling off respawns the player and counts a fall.
        editor.call("execute_lua", code="Scene.FindEntityByName('Player'):SetPosition(vec3(30, -20, 0))")
        editor.call("run_frames", frames=5)
        assert editor.call("execute_lua", code=f"{game}.falls")["result"] == "1"

        state = editor.call("get_play_state")
        assert not state["script_errors"], state["script_errors"]
        shot = editor.call("capture", path=os.path.abspath(args.screenshot), width=1280, height=720)
        editor.call("stop")
        print("Play-test passed: player moves, orbs are collected, falls respawn. Screenshot:", shot["path"])

        editor.call("set_project", WindowWidth=1280, WindowHeight=720)
        if args.export:
            result = editor.call("export_game", output=os.path.abspath(args.export))
            print("Exported:", result["executable"])


if __name__ == "__main__":
    main()
