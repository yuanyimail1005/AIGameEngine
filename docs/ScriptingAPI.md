# Lua scripting API

Game behaviour in Ember is written in Lua 5.4. A script is attached to an entity with a `Script` component
(`{"Script": "Scripts/Player.lua"}`); every entity gets its own instance. The feature test scene
(`TestProject/Assets/Scripts/FeatureTest.lua`) exercises every function listed here.

## Script structure

A script file returns a table. Its functions become methods of every instance; `self.entity` is the entity the
script is attached to, and anything stored on `self` is per-instance state.

```lua
local Player = {}

function Player:OnCreate()                -- once, when play mode starts (or when the entity is spawned)
	self.speed = 5
end

function Player:OnUpdate(dt)              -- every frame, before physics; dt in seconds
	local move = vec3(0, 0, 0)
	if Input.IsKeyDown("A") then move.x = move.x - 1 end
	if Input.IsKeyDown("D") then move.x = move.x + 1 end
	self.entity:Translate(move * self.speed * dt)
end

function Player:OnCollisionBegin(other)   -- other is an Entity
	if other.name == "Coin" then other:Destroy() end
end

return Player
```

| Callback | When |
|---|---|
| `OnCreate()` | Instance created (play start, entity spawned, or script assigned at runtime) |
| `OnUpdate(dt)` | Every frame, before the physics step |
| `OnLateUpdate(dt)` | Every frame, after physics and collision callbacks |
| `OnDestroy()` | Entity destroyed, script removed, or play mode stopped |
| `OnCollisionBegin(other)` / `OnCollisionEnd(other)` | Solid contact with another body starts / ends |
| `OnTriggerBegin(other)` / `OnTriggerEnd(other)` | A body enters / leaves a trigger (or this trigger touches a body) |

Order: scripts update in entity creation order. Entities destroyed during a frame are removed at the end of that
frame. If a callback raises an error, the error is logged with a stack trace and that instance stops receiving
callbacks (other scripts keep running).

Scripts run in a sandbox: no `io`, no `os.execute`/`os.remove`, no `load`/`loadfile`/`dofile`. `os.clock`,
`os.time`, `os.date`, `string`, `table`, `math`, `utf8` and `coroutine` are available.

## Modules

`require("Scripts.Lib.Utils")` loads `Assets/Scripts/Lib/Utils.lua` once and returns its value. Module names are
dotted identifiers (letters, digits, `_`).

## Math

| | |
|---|---|
| `vec3(x, y, z)`, `vec3(s)`, `vec3()` | 3D vector; fields `x`, `y`, `z` |
| `vec2(x, y)` | 2D vector; fields `x`, `y` |
| `a + b`, `a - b`, `a * b`, `a * s`, `s * a`, `a / b`, `a / s`, `-a`, `a == b` | Component-wise operators |
| `v:length()`, `v:normalized()`, `a:dot(b)` | vec2 and vec3 |
| `a:cross(b)`, `a:distance(b)`, `a:lerp(b, t)` | vec3 |
| `Math.Lerp(a, b, t)`, `Math.Clamp(v, lo, hi)` | Scalars |
| `Math.Radians(deg)`, `Math.Degrees(rad)` | Angle conversion |
| `Math.Random([lo, hi])`, `Math.RandomInt(lo, hi)`, `Math.Seed(n)` | Deterministic per-scene random numbers |

Coordinate system: right-handed, +Y up, entities face -Z (cameras look down -Z, lights shine along -Z).
Rotations are XYZ Euler angles in radians unless the function name says `Degrees`.

## Entity

Entities are handles; a handle to a destroyed entity is safe to use (`IsValid()` returns false, getters return
defaults, setters do nothing).

| Member | Description |
|---|---|
| `entity.id` | 64-bit id (integer) |
| `entity.name` | Name (read / write) |
| `IsValid()` | False once destroyed |
| `Destroy()` | Destroy at the end of the frame (children too) |
| **Transform (local)** | |
| `GetPosition()`, `SetPosition(v)`, `Translate(v)` | |
| `GetRotation()`, `SetRotation(v)`, `Rotate(v)` | Radians |
| `GetRotationDegrees()`, `SetRotationDegrees(v)` | Degrees |
| `GetScale()`, `SetScale(v)` | |
| **Transform (world)** | |
| `GetWorldPosition()`, `SetWorldPosition(v)` | Includes parents |
| `GetForward()`, `GetRight()`, `GetUp()` | World-space unit vectors (-Z, +X, +Y) |
| `LookAt(target [, up])` | Turn so that forward points at `target` |
| **Hierarchy** | |
| `GetParent()`, `SetParent(entity or nil)`, `GetChildren()` | Re-parenting keeps the world transform |
| **Components** (any component, by name) | |
| `HasComponent(name)` | |
| `GetComponent(name)` | Table copy of the data, e.g. `GetComponent("PointLight").Range` |
| `AddComponent(name [, values])`, `SetComponent(name, values)` | Patch: only given fields change; returns success |
| `RemoveComponent(name)` | Built-in components (Transform, Tag) cannot be removed |
| `SetColor(r, g, b [, a])` | Shortcut: enables the MeshRenderer material override with this base color |
| `SetVisible(bool)` | Shortcut for `MeshRenderer.Visible` |
| **Physics** (entities with colliders) | |
| `AddForce(v)`, `AddImpulse(v)`, `AddTorque(v)` | Dynamic bodies |
| `SetVelocity(v)`, `GetVelocity()` | Linear velocity |
| `SetAngularVelocity(v)`, `GetAngularVelocity()` | Radians per second |
| `RefreshPhysics()` | Rebuild the body after changing collider or rigid body settings |
| **Audio** (entities with an AudioSource) | |
| `PlaySound()`, `StopSound()`, `IsSoundPlaying()` | |
| **Scripts** | |
| `GetScript()` | The entity's script instance (call its methods, read its fields) or nil |

Setting the transform of a dynamic body teleports it; kinematic bodies follow their transform smoothly.

Component names and fields are the same as in the editor and the AI interface (`list_component_types`):
`Transform`, `Camera`, `MeshRenderer`, `DirectionalLight`, `PointLight`, `SpotLight`, `SkyLight`, `RigidBody`,
`BoxCollider`, `SphereCollider`, `CapsuleCollider`, `Script`, `AudioSource`, `AudioListener`, `Prefab`, `Tag`.
In tables returned by `GetComponent`, 3-element vectors are `vec3`, 2-element vectors `vec2`, and other arrays
(e.g. RGBA colors) are Lua arrays (`color[1]` is red).

## Scene

| Function | Description |
|---|---|
| `Scene.CreateEntity([name])` | New entity at the origin |
| `Scene.Instantiate(prefabPath [, position [, parent]])` | Spawn a prefab (`Prefabs/Enemy.eprefab`); returns the root or nil |
| `Scene.Destroy(entity)` | Same as `entity:Destroy()` |
| `Scene.FindEntityByName(name)` | First entity with that name, or nil |
| `Scene.GetEntityByID(id)` | |
| `Scene.GetEntities()` | All entities (creation order) |
| `Scene.GetEntitiesWith(componentName)` | Entities that have a component |
| `Scene.GetEntityCount()` | |
| `Scene.GetPrimaryCamera()` | |
| `Scene.GetTime()` | Seconds since play started |
| `Scene.Raycast(origin, direction [, maxDistance])` | Closest hit `{entity, point, normal, distance}` or nil; triggers are ignored |
| `Scene.SetGravity(v)`, `Scene.GetGravity()` | Default `vec3(0, -9.81, 0)` |

## Input

Key names: `A`-`Z`, `0`-`9`, `Space`, `Enter`, `Escape`, `Tab`, `Backspace`, `Left`, `Right`, `Up`, `Down`,
`LeftShift`, `LeftControl`, `LeftAlt`, `F1`-`F12`, `KP0`-`KP9`, ... (case-insensitive). Mouse buttons: `Left`,
`Right`, `Middle`.

| Function | Description |
|---|---|
| `Input.IsKeyDown(key)` | Held |
| `Input.IsKeyPressed(key)` | Went down this frame |
| `Input.IsKeyReleased(key)` | Went up this frame |
| `Input.IsMouseButtonDown(b)`, `IsMouseButtonPressed(b)`, `IsMouseButtonReleased(b)` | |
| `Input.GetMousePosition()`, `Input.GetMouseDelta()`, `Input.GetScrollDelta()` | `vec2`, window pixels |

## Audio

| Function | Description |
|---|---|
| `Audio.PlayOneShot(clip [, volume [, position]])` | Fire-and-forget sound; positional if `position` is given |
| `Audio.SetMasterVolume(v)`, `Audio.GetMasterVolume()` | |

Supported formats: WAV, MP3, FLAC. Without an audio device (servers, CI) sounds play silently.

## Logging and tests

| Function | Description |
|---|---|
| `Log.Info(...)`, `Log.Warn(...)`, `Log.Error(...)`, `print(...)` | Editor console / log |
| `Test.Check(condition, name [, message])` | Records a pass/fail result |
| `Test.Pass(name)`, `Test.Fail(name [, message])` | |
| `Test.Expect(name)` | Declares a check that must pass before the run ends |

`EmberEditor --project P --run-tests` plays the start scene and fails if any check fails, an expected check is
missing, or any script or GPU error occurs.
