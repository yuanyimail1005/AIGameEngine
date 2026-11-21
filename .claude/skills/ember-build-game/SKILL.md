---
name: ember-build-game
description: Build, test and export a game with the Ember engine by driving the editor through its AI control interface (headless, no display needed). Use when asked to make a game, level, scene, prototype or demo with Ember, or to modify an existing Ember project.
---

# Building a game with Ember

You control the Ember editor through JSON commands. You never need the editor UI or a display: the editor runs
headless, renders offscreen, and you look at results through captured PNG images.

References: `docs/AIControl.md` (all commands, protocol), `docs/ScriptingAPI.md` (Lua API).
Working examples: `Tools/build_test_project.py` (scene built through the API), `TestProject/Assets/Scripts/`.

## 1. Start the editor

Build first if needed (`cmake --build build -j4`). Then use the Python client from a script you write:

```python
import sys; sys.path.insert(0, "Tools")
from ember_ai import EmberEditor, EmberError
editor = EmberEditor.launch()            # headless editor on a free port; editor.close() quits it
editor.call("new_project", path="/abs/path/MyGame", name="MyGame")   # or open_project
```

Prefer writing one Python build script per game (re-runnable, like `Tools/build_test_project.py`) over many
one-off calls. For interactive exploration: `python3 Tools/ember_ai.py launch --port 7820` in the background, then
`python3 Tools/ember_ai.py call <method> '<json>'`.

## 2. Build the scene

- New projects contain `Scenes/Main.escene` with a camera, a sun and a sky. Inspect with `list_entities`.
- Create objects with components in one call:
  ```python
  editor.call("create_entity", name="Player", components={
      "Transform": {"Translation": [0, 1, 0]},
      "MeshRenderer": {"Mesh": "builtin://Capsule", "MaterialOverride": {"BaseColor": [0.2, 0.5, 1.0]}},
      "RigidBody": {"Type": "Dynamic", "LockRotation": True}, "CapsuleCollider": {},
      "Script": {"Script": "Scripts/Player.lua"}})
  ```
- `list_component_types` shows every component's fields and defaults. Fields you omit keep their defaults;
  `set_component` patches existing components.
- Static level geometry = MeshRenderer + collider, no RigidBody. Moving platforms = `RigidBody.Type: Kinematic`.
  Pickups / zones = `RigidBody.IsTrigger: true` (script `OnTriggerBegin`).
- Collider sizes are multiplied by the entity scale (a unit `BoxCollider` matches `builtin://Cube` at any scale).
- Repeated objects: build one, `create_prefab`, then spawn from scripts with `Scene.Instantiate`.
- Lighting: one `DirectionalLight` (shadows), a `SkyLight` (image-based lighting; optional `EnvironmentMap` .hdr),
  `PointLight`/`SpotLight` for local light, emissive materials for glowing objects.
- Use `UVTiling` on textured materials for large surfaces. Import external files with `import_file`.

## 3. Write gameplay scripts

`write_asset(path="Scripts/Player.lua", content=...)`. A script returns a table with `OnCreate`, `OnUpdate(dt)`,
`OnCollisionBegin(other)`, etc. Keep game-wide state in a manager entity's script; reach it from other scripts with
`Scene.FindEntityByName("GameManager"):GetScript()`. Use `Log.Info` for debugging and read logs with `get_logs`.

## 4. Test like a player, verify like an engineer

```python
editor.call("run_frames", frames=60)                        # starts play mode, deterministic
editor.call("send_input", key="D", action="press"); editor.call("run_frames", frames=30)
editor.call("send_input", key="D", action="release")
print(editor.call("execute_lua", code="Scene.FindEntityByName('Player'):GetPosition()"))
state = editor.call("get_play_state")                      # script_errors must be empty
shot = editor.call("capture", path="/tmp/frame.png")        # then LOOK at the image (Read tool)
editor.call("stop")                                         # discard runtime changes before editing again
```

Always check `get_play_state()["script_errors"]` and look at at least one captured frame after changes: a scene
can be logically correct and still render black, off-camera or tiny. If something is not visible, check the camera
transform and lights first (`get_entity`, `capture` with `camera="editor"` after `focus_entity`).

Add `Test.Check`/`Test.Expect` calls to a test script and run `EmberEditor --project <dir> --run-tests` for an
automated pass/fail of game logic.

## 5. Save and ship

```python
editor.call("save_scene", path="Scenes/Main.escene")
editor.call("set_project", StartScene="Scenes/Main.escene", WindowWidth=1280, WindowHeight=720)
editor.call("export_game", output="/abs/path/MyGame_Build")   # must be outside the project directory
```

Verify the export headless: `<build>/<Name> --headless --frames 120 --screenshot /tmp/exported.png`.

## Pitfalls

- Play mode edits are discarded on `stop`; always `stop` before authoring. `undo`/`redo` work in edit mode only.
- `run_frames` starts play mode automatically; scripts written after play started are not reloaded until the next play.
- Ids are strings in responses; entity names work wherever an id is accepted (first match by creation order).
- Asset paths are relative to `Assets/` and must not contain `..`.
- The Raspberry Pi GPU is slow: keep captures small (e.g. 640x360) while iterating.
