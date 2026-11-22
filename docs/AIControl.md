# AI control interface

The Ember editor is fully controllable by AI agents (and scripts, CI jobs, external tools). Everything the editor
UI can do is available as a JSON command: create projects, build scenes, write Lua scripts, play and test the game
deterministically, look at rendered frames, and export a standalone build. An agent can go from an empty folder to
a playable, exported game without a human touching the editor.

## Connecting

Start the editor with the AI server. Headless mode needs no display (offscreen rendering) and is what agents
normally use:

```bash
./build/bin/EmberEditor --headless --ai-server            # port 7820
./build/bin/EmberEditor --headless --ai-server 0          # pick a free port
./build/bin/EmberEditor --ai-server --project MyGame      # with the UI, controllable at the same time
```

When the server is ready the editor prints `EMBER_AI_SERVER_READY port=<port>` on stdout. The server only listens
on `127.0.0.1`.

**Protocol**: one JSON object per line in each direction (TCP).

```json
{"id": 1, "method": "create_entity", "params": {"name": "Box", "components": {"MeshRenderer": {"Mesh": "builtin://Cube"}}}}
{"id": 1, "result": {"id": "8312...", "name": "Box", "parent": null, "children": [], "components": ["Transform", "MeshRenderer"]}}
{"id": 2, "method": "get_entity", "params": {"entity": "Nope"}}
{"id": 2, "error": {"message": "no entity named 'Nope'"}}
```

Requests on one connection are processed in order, on the editor's main thread between frames.

### Python client

`Tools/ember_ai.py` (standard library only) wraps the protocol:

```python
import sys; sys.path.insert(0, "Tools")
from ember_ai import EmberEditor

with EmberEditor.launch() as editor:                 # starts a headless editor, quits it on exit
    editor.call("new_project", path="/tmp/MyGame", name="MyGame")
    box = editor.call("create_entity", name="Box", components={"MeshRenderer": {"Mesh": "builtin://Cube"}})
    editor.call("capture", path="/tmp/shot.png")      # look at the result
```

Command line: `python3 Tools/ember_ai.py call get_info`, `... batch commands.jsonl`, `... commands`.

### Batch files

`EmberEditor --batch file.jsonl` executes one request per line (lines starting with `#` are comments), prints each
response and exits with code 1 if any command failed. Useful for reproducible scene builds and CI.

## Concepts

- **Entities** are addressed by id or by name. Ids are 64-bit integers returned as decimal strings (to survive
  JSON tools that use doubles); pass them back as strings or integers. Names resolve to the first entity
  (in creation order) with that name.
- **Components** are plain data objects. `list_component_types` returns every component with its fields and
  default values. `set_component` / `add_component` *patch*: only the fields you pass change. Vectors are arrays
  (`[x, y, z]`), enums are strings (`"Dynamic"`), colors are `[r, g, b]` or `[r, g, b, a]` in linear 0..1.
  Convenience fields: `Transform.RotationDegrees`, `Camera.PerspectiveFOVDegrees`.
- **Asset paths** are relative to the project's `Assets/` directory (`Scripts/Player.lua`). Built-in meshes:
  `builtin://Cube`, `Sphere`, `Plane`, `Cylinder`, `Capsule`. Paths containing `..` or absolute paths are rejected.
- **Edit vs play mode**: commands act on the *active* scene. In edit mode that is the scene being authored
  (changes are undoable and saved with `save_scene`). In play mode it is a runtime copy; `stop` discards it.
- **Deterministic testing**: `run_frames` simulates N frames immediately at a fixed time step, so an agent can
  write a script, run 120 frames, and inspect positions, script variables (`execute_lua`) or a captured image.
  `send_input` presses/releases keys and buttons as if from the keyboard.
- **Seeing the game**: `capture` renders the active scene to a PNG (with the scene camera or the editor camera)
  and returns its absolute path. `pick_entity` tells which entity is at a pixel of the last capture.

## Typical workflow for building a game

1. `new_project` (creates `Assets/`, a start scene with camera, sun and sky).
2. Build the level: `create_entity` with components; reusable objects become prefabs (`create_prefab`).
3. Write behaviour: `write_asset` a Lua script (see [ScriptingAPI.md](ScriptingAPI.md)), attach it with a
   `Script` component.
4. Test: `run_frames`, `send_input`, `execute_lua` to query state, `get_play_state` for script errors,
   `capture` to look at frames. `stop`, fix, repeat.
5. `save_scene`, `set_project` (`StartScene`, window size), `export_game` to a folder outside the project.

## Command reference

Regenerate with `python3 Tools/gen_command_docs.py` after adding or changing commands.

<!-- BEGIN GENERATED COMMAND REFERENCE -->

54 commands. Parameters marked *optional* may be omitted.

### General

#### `get_info`

Engine, project, scene and play-state summary.

#### `get_logs`

Returns log messages newer than 'since' (a sequence number from a previous call).

| Parameter | Description |
|---|---|
| `min_level` | string *(optional)* - trace\|info\|warn\|error |
| `since` | integer *(optional)* - last sequence number seen (default 0) |

#### `list_commands`

Lists all commands with descriptions and parameters.

| Parameter | Description |
|---|---|
| `category` | string *(optional)* - filter |

#### `ping`

Checks the connection.

#### `quit`

Closes the editor.

#### `redo`

Redoes the last undone edit (edit mode).

#### `undo`

Undoes the last edit (edit mode).

### Project

#### `get_project`

Returns the project settings.

#### `new_project`

Creates a project (directory with Assets/ and a start scene) and opens it.

| Parameter | Description |
|---|---|
| `name` | string - project name |
| `path` | string - project directory |

#### `open_project`

Opens a project (.eproject file or its directory) and its start scene.

| Parameter | Description |
|---|---|
| `path` | string |

#### `set_project`

Changes project settings (any subset) and saves the project file.

| Parameter | Description |
|---|---|
| `Fullscreen` | boolean |
| `Name` | string |
| `RenderQuality` | string - Auto\|Low\|High |
| `StartScene` | string - asset path |
| `VSync` | boolean |
| `WindowHeight` | integer |
| `WindowWidth` | integer |

### Scene

#### `get_scene`

Returns the active scene as YAML text (the .escene format).

#### `load_scene_data`

Replaces the edited scene with the given .escene YAML text (undoable).

| Parameter | Description |
|---|---|
| `yaml` | string - scene in .escene format |

#### `new_scene`

Replaces the edited scene with an empty one (unsaved).

| Parameter | Description |
|---|---|
| `name` | string *(optional)* |

#### `open_scene`

Opens a scene file.

| Parameter | Description |
|---|---|
| `path` | string - asset path, e.g. Scenes/Level1.escene |

#### `save_scene`

Saves the edited scene (to 'path', or its current file).

| Parameter | Description |
|---|---|
| `path` | string *(optional)* - asset path |

### Entity

#### `create_entity`

Creates an entity, optionally with a parent and components.

| Parameter | Description |
|---|---|
| `components` | object *(optional)* - {"MeshRenderer": {"Mesh": "builtin://Cube"}, "Transform": {"Translation": [0,1,0]}} |
| `name` | string *(optional)* |
| `parent` | id or name *(optional)* |

#### `destroy_entity`

Destroys an entity and its children.

| Parameter | Description |
|---|---|
| `entity` | id or name |

#### `duplicate_entity`

Duplicates an entity (with children); returns the copy.

| Parameter | Description |
|---|---|
| `entity` | id or name |

#### `find_entities`

Finds entities by name substring and/or component.

| Parameter | Description |
|---|---|
| `component` | string *(optional)* |
| `name` | string *(optional)* - case-sensitive substring |

#### `get_entity`

Returns an entity with all component data.

| Parameter | Description |
|---|---|
| `entity` | id or name |

#### `list_entities`

Lists all entities of the active scene (id, name, parent, children, component names).

#### `rename_entity`

Renames an entity.

| Parameter | Description |
|---|---|
| `entity` | id or name |
| `name` | string |

#### `select_entity`

Selects an entity in the editor (null clears the selection).

| Parameter | Description |
|---|---|
| `entity` | id or name, or null |

#### `set_parent`

Re-parents an entity (parent null = root).

| Parameter | Description |
|---|---|
| `entity` | id or name |
| `keep_world_transform` | boolean *(optional)* (default true) |
| `parent` | id or name, or null |

### Component

#### `add_component`

Adds a component (or updates it if present) and returns its data.

| Parameter | Description |
|---|---|
| `component` | string |
| `entity` | id or name |
| `values` | object *(optional)* - fields to set (others unchanged) |

#### `get_component`

Returns one component's data.

| Parameter | Description |
|---|---|
| `component` | string |
| `entity` | id or name |

#### `list_component_types`

Lists component types with their fields and default values.

#### `remove_component`

Removes a component.

| Parameter | Description |
|---|---|
| `component` | string |
| `entity` | id or name |

#### `set_component`

Sets component fields (adds the component if missing) and returns its data.

| Parameter | Description |
|---|---|
| `component` | string |
| `entity` | id or name |
| `values` | object *(optional)* - fields to set (others unchanged) |

#### `set_transform`

Sets local transform values (any subset).

| Parameter | Description |
|---|---|
| `entity` | id or name |
| `position` | [x,y,z] *(optional)* |
| `rotation` | [x,y,z] radians *(optional)* |
| `rotation_degrees` | [x,y,z] *(optional)* |
| `scale` | [x,y,z] or number *(optional)* |

### Prefab

#### `create_prefab`

Saves an entity (and children) as a prefab asset.

| Parameter | Description |
|---|---|
| `entity` | id or name |
| `path` | string - asset path, e.g. Prefabs/Enemy.eprefab |

#### `instantiate_prefab`

Instantiates a prefab into the active scene.

| Parameter | Description |
|---|---|
| `parent` | id or name *(optional)* |
| `path` | string - asset path |
| `position` | [x,y,z] *(optional)* |

### Asset

#### `create_material`

Writes a material asset (.ematerial) usable by MeshRenderer.Material.

| Parameter | Description |
|---|---|
| `path` | string - asset path, e.g. Materials/Gold.ematerial |
| `values` | object - BaseColor, Metallic, Roughness, EmissiveColor, EmissiveStrength, AlphaMode, *Texture, ... |

#### `delete_asset`

Deletes an asset file or directory.

| Parameter | Description |
|---|---|
| `path` | string |

#### `import_file`

Copies an external file (model, texture, HDRI, audio, font) into the asset directory.

| Parameter | Description |
|---|---|
| `destination` | string - asset path |
| `source` | string - absolute path of the file to import |

#### `list_assets`

Lists files in the asset directory (or a sub-directory).

| Parameter | Description |
|---|---|
| `directory` | string *(optional)* - asset sub-directory |
| `recursive` | boolean *(optional)* (default true) |

#### `read_asset`

Returns the text content of an asset (scripts, scenes, prefabs, materials).

| Parameter | Description |
|---|---|
| `path` | string |

#### `write_asset`

Creates or overwrites a text asset (e.g. a Lua script). Directories are created as needed.

| Parameter | Description |
|---|---|
| `content` | string |
| `path` | string - asset path |

### Play

#### `execute_lua`

Runs Lua code in the running game's script environment and returns the result.

| Parameter | Description |
|---|---|
| `code` | string |

#### `get_play_state`

Returns play state, simulated time and script diagnostics.

#### `pause`

Pauses or resumes play mode.

| Parameter | Description |
|---|---|
| `paused` | boolean *(optional)* (default true) |

#### `play`

Enters play mode (simulates a copy of the edited scene).

#### `run_frames`

Simulates N frames immediately (deterministic; starts play mode if needed).

| Parameter | Description |
|---|---|
| `dt` | number *(optional)* - seconds per frame (default 1/60) |
| `frames` | integer |

#### `send_input`

Injects keyboard/mouse input into the game (as if from the window).

| Parameter | Description |
|---|---|
| `action` | string - press\|release |
| `key` | string *(optional)* - e.g. W, Space, Left, Escape |
| `mouse_button` | string *(optional)* - Left\|Right\|Middle |
| `mouse_position` | [x,y] *(optional)* - move the mouse |
| `scroll` | number *(optional)* |

#### `step`

Pauses and advances play mode by N frames on subsequent editor frames.

| Parameter | Description |
|---|---|
| `frames` | integer *(optional)* (default 1) |

#### `stop`

Leaves play mode (runtime changes are discarded).

### Render

#### `capture`

Renders the active scene to a PNG file; returns its path and render statistics.

| Parameter | Description |
|---|---|
| `camera` | string *(optional)* - 'editor' or 'game' (default: game when playing or when the scene has a camera) |
| `height` | integer *(optional)* (default 540) |
| `path` | string *(optional)* - output file (default: <project>/Screenshots/capture_N.png) |
| `width` | integer *(optional)* (default 960) |

#### `focus_entity`

Points the editor camera at an entity.

| Parameter | Description |
|---|---|
| `entity` | id or name |

#### `get_render_settings`

Returns renderer quality settings.

#### `pick_entity`

Returns the entity at a pixel of the last capture or viewport render.

| Parameter | Description |
|---|---|
| `source` | string *(optional)* - 'capture' (default) or 'viewport' |
| `x` | integer |
| `y` | integer |

#### `set_editor_camera`

Positions the editor camera.

| Parameter | Description |
|---|---|
| `position` | [x,y,z] |
| `target` | [x,y,z] |

#### `set_render_settings`

Changes renderer settings (any subset of get_render_settings fields).

| Parameter | Description |
|---|---|
| `...` | fields from get_render_settings |

### Build

#### `export_game`

Exports the project as a standalone game (runtime + assets) into a directory.

| Parameter | Description |
|---|---|
| `output` | string - destination directory (outside the project) |

<!-- END GENERATED COMMAND REFERENCE -->
