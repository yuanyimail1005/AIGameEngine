---
name: ember-engine-dev
description: Checklists for changing the Ember engine itself - adding components, Lua API functions, editor/AI commands, renderer features or shaders, and the build/test/commit routine. Use when modifying code under Ember/, EmberEditor/, EmberRuntime/ or EmberTests/.
---

# Developing the Ember engine

Read `AGENTS.md` first (layout, style, rules). This skill holds the step-by-step checklists.

## Build and test routine (every change)

```bash
cmake --build build -j4                                   # zero warnings (warnings are errors)
./build/bin/EmberTests                                    # unit + GPU tests on the real GPU
EMBER_GPU=llvmpipe ./build/bin/EmberTests                 # renderer changes: also on the CPU rasterizer
./build/bin/EmberEditor --project TestProject --run-tests --frames 400   # feature scene
(cd build && ctest --output-on-failure)                   # everything registered with CTest
```

Useful: `EMBER_TEST_VERBOSE=1`, `-tc="*pattern*"`, `-ts=Renderer`, `EMBER_TEST_SAVE_IMAGES=/tmp/dir` (dumps every
renderer-test frame as PNG; look at them).

## Adding a component

1. Struct in `Ember/src/Ember/Scene/Components.h` (plain data, defaults for every field) and add it to `AllComponents`.
2. Register in `ComponentRegistry.cpp`: `ToNode` / `FromNode` (patch semantics: only present keys; write string
   fields with `MakeStringNode` so the AI/Lua converters never turn text like "42" into a number), and a
   `MakeInfo<T>("Name", "Category")` line. This alone makes it work in serialization, prefabs, the inspector,
   Lua `Get/Set/AddComponent` and the AI interface.
3. Implement the behaviour in the owning system (renderer / physics / audio / scripts) - read the component each
   frame so runtime changes apply.
4. Tests: serialization round trip happens automatically for all registered components; add behaviour tests.
5. Add it to `Tools/build_test_project.py` and check it in `TestProject/Assets/Scripts/FeatureTest.lua`; rebuild
   the test project (`python3 Tools/build_test_project.py`) and run `--run-tests`.
6. Document new fields in `docs/ScriptingAPI.md` if scripts commonly use them.

## Adding a Lua API function

1. Bind it in `Ember/src/Ember/Scripting/ScriptEngine.cpp` (`RegisterEntity` / `RegisterGlobals` / `RegisterMath`).
   Entity functions take `const LuaEntity&` and must tolerate destroyed entities. Report user mistakes with
   `ReportError` (counts as a script error) - never crash or assert on script input.
2. Paths from scripts go through `ResolveAsset` (rejects absolute paths and `..`).
3. Test in `EmberTests/src/Scripting/ScriptTests.cpp`, call it from `FeatureTest.lua`, document in
   `docs/ScriptingAPI.md`.

## Adding an editor / AI command

1. Register in `EmberEditor/Core/EditorCommands.cpp` with a category, description and parameter docs.
   Use `JsonUtils` helpers for parameters (they throw `CommandError` with good messages). Call
   `m_Context.RecordUndo(...)` before mutating the edited scene. Validate asset paths with `ValidatedAssetPath`.
2. If the UI needs the operation, call the command from `EditorUI.cpp` (keeps UI and AI behaviour identical).
3. Test in `EmberTests/src/Editor/EditorTests.cpp`.
4. Regenerate the reference: `python3 Tools/gen_command_docs.py` (updates `docs/AIControl.md`).

## Renderer and shader changes

- Shaders: `Ember/shaders/<Name>.<vert|frag|comp>.glsl`, includes in `shaders/include/`. Compiled to SPIR-V at
  build time and embedded; load with `ShaderLibrary::Get("Name", type)`.
- Binding model: descriptor set 0 = frame resources, set 1 = material (`registerSpaceIsDescriptorSet`), NVRHI binding
  offsets are zero so GLSL `binding = N` equals the NVRHI slot. Push constants <= 128 bytes (NVRHI limit).
- C++ structs mirroring std140 blocks have `static_assert(sizeof(...))` next to them - update both sides.
- Pi 5 (V3DV) constraints: <= 16 sampled images and 4 storage images per stage, compute workgroups <= 256
  invocations, no BC textures, depth formats may lack linear filtering (sample depth with point samplers or
  comparison samplers), no `HOST_CACHED` memory (handled by `cmake/patches/nvrhi-host-cached-fallback.patch`).
- Every GPU test ends with `CHECK(gpu->GetErrorCount() == 0)`; the Khronos validation layer is active in tests.
- Never open a command list you might not submit (leaks the Vulkan command buffer) - validate inputs first.
- Only one immediate command list may be open at a time. `RenderAssets` loads (models, textures, fonts) submit their
  own uploads, so resolve every asset a pass needs before the frame's command list is opened (see
  `UIRenderer::Prepare` / `Render`).
- Binary resources the engine needs at runtime (e.g. the default font) are embedded with `ember_embed_resources` in
  `Ember/CMakeLists.txt` and read with `EmbeddedResources::Find("<Name>")`.

## Committing

Run the full routine above, review the diff against `AGENTS.md`, commit with a message explaining what and why,
push to `main`.
