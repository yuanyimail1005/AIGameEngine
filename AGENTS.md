# AGENTS.md — Ember Engine development guide

Ember is a production-grade, simple 3D game engine (C++20, CMake, GLFW, NVRHI/Vulkan, glm, miniaudio, Lua,
Jolt, EnTT, Dear ImGui). It targets Windows, macOS (MoltenVK), Linux and Raspberry Pi OS (Pi 5, V3DV driver).
This file is the contract for anyone — human or AI agent — changing the code.

## Repository layout

| Path | Purpose |
|---|---|
| `Ember/src/Ember/` | Core engine static library (`Ember`). Subfolders: `Core`, `Renderer`, `Scene`, `Asset`, `Physics`, `Scripting`, `Audio`, `ImGui`, `Editor` (editor-only services). |
| `Ember/shaders/` | GLSL shaders `<Name>.<vert|frag|comp>.glsl`, compiled to SPIR-V at build time and **embedded** in the binary. Shared includes in `Ember/shaders/include/`. |
| `Ember/vendor/` | Single-TU implementations of header-only libs (stb, cgltf, miniaudio). Built without strict warnings. |
| `EmberEditor/Core/` | `EmberEditorCore` library: `EditorContext` (editor state), `EditorCommands` (JSON command API), `AIServer`, `GameExporter`. UI-independent; linked by the tests. |
| `EmberEditor/App/` | Editor executable: ImGui UI (`EditorUI.cpp`), command-line modes (`EditorApp.cpp`). |
| `EmberRuntime/` | Runtime executable that plays an exported game. |
| `EmberTests/` | doctest unit + integration tests (`EmberTests` executable). |
| `cmake/` | Dependency pinning (`EmberDependencies.cmake`), shader embedding, compile options, patches for third-party code. |
| `TestProject/` | Feature test project (every component + whole Lua API). Built by `Tools/build_test_project.py`. |
| `Tools/` | `ember_ai.py` (AI client), `build_test_project.py`, `generate_test_assets.py`, `gen_command_docs.py`. |
| `docs/` | `ScriptingAPI.md`, `AIControl.md` (command reference generated from `list_commands`). |
| `.claude/skills/` | Agent skills: `ember-build-game` (making games via the AI interface), `ember-engine-dev` (checklists for engine changes). |

## Building

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug   # Debug | Release | RelWithDebInfo | Dist
cmake --build build -j4
./build/bin/EmberTests                                   # must pass before every commit
```

* First configure downloads all dependencies (pinned tags/commits) — takes several minutes on a Pi.
* Linux system packages: `libvulkan-dev glslang-tools ninja-build libwayland-dev libxkbcommon-dev xorg-dev`
  (`vulkan-validationlayers` recommended — tests run with validation on and fail on validation errors).
* `Dist` = shipping config: asserts off, `EMBER_DIST` defined, editor tooling compiled out.
* Warnings are errors for Ember targets (`EMBER_WARNINGS_AS_ERRORS=ON`). Do not disable it; fix the warning.
  Third-party code is included as `SYSTEM` so it never triggers Ember warnings.

## Testing rules

* Every feature lands with tests in `EmberTests/src/<Area>/`. Bug fixes land with a regression test.
* GPU tests use the shared headless device: `EMBER_REQUIRE_GPU(gpu);` and must end with
  `CHECK(gpu->GetErrorCount() == 0);` — this counts NVRHI and Vulkan validation errors.
* Rendering tests read pixels back with `ReadbackTexture()` and assert on values; never rely on eyeballing.
* `EMBER_GPU=llvmpipe ./build/bin/EmberTests` runs GPU tests on the CPU rasterizer (CI without a GPU).
  Both the real GPU and llvmpipe must pass.
* `EMBER_TEST_VERBOSE=1` shows engine trace logging during tests.
* Run a single test: `./build/bin/EmberTests -tc="*name pattern*"`; a suite: `-ts=Renderer`.
* The feature test scene (`TestProject/`) must exercise every component and the entire scripting API; extend it
  with every new feature (`Tools/build_test_project.py` + `TestProject/Assets/Scripts/FeatureTest.lua`) and keep
  `EmberEditor --project TestProject --run-tests --frames 400` green. `ctest` runs it together with `EmberTests`.
* `EMBER_TEST_SAVE_IMAGES=<dir>` dumps every renderer-test frame as PNG - look at them when changing rendering.

## Code style (Hazel conventions)

* Files `PascalCase.h/.cpp`, one primary class per file. `#pragma once`. Tabs for indentation, Allman braces.
* Everything lives in `namespace Ember` (nested namespaces sparingly). Namespace contents are indented.
* Types, functions and methods: `PascalCase`. Locals and parameters: `camelCase`.
* Members `m_PascalCase`, statics `s_PascalCase`, globals `g_PascalCase`, constants `c_PascalCase` or `constexpr`.
* Public struct fields (plain data, components, specs): `PascalCase` without prefix.
* Macros: `EMBER_UPPER_CASE`. Enums: `enum class` with `PascalCase` values.
* Ownership: `Scope<T>` (unique_ptr) and `Ref<T>` (shared_ptr) from `Ember/Core/Base.h`; raw pointers are
  non-owning. GPU objects use NVRHI handles (`nvrhi::TextureHandle`, ...).
* Include order: own header, Ember headers, third-party, standard library — each group separated by a blank line.
* Logging: `EMBER_CORE_*` inside the engine, `EMBER_*` for client/editor/script code. No `std::cout`.
* Invariants: `EMBER_CORE_ASSERT` (debug only) / `EMBER_CORE_VERIFY` (kept in release). Recoverable failures
  (missing files, bad user data, script errors) are logged and reported, never asserted.

## Engineering rules

* No hacks or shortcuts. If a workaround is needed for a driver/library, isolate it and document why
  (see `cmake/patches/` for third-party patches; each one must explain itself).
* All third-party versions are pinned in `cmake/EmberDependencies.cmake`. Upgrades are deliberate commits.
* The renderer must run on the Raspberry Pi 5 (V3DV): no bindless/descriptor indexing, no BC textures (use
  uncompressed or ETC2), no multi-draw-indirect, no `shaderDrawParameters`, 16 bound descriptor sets max.
  NVRHI's viewport uses D3D conventions (y-up NDC, depth 0..1); glm is configured with `GLM_FORCE_DEPTH_ZERO_TO_ONE`.
* Vulkan calls outside NVRHI go through the Vulkan-Hpp dynamic dispatcher's C entry points
  (`VulkanGraphicsDevice.cpp`). Never define the dispatcher storage anywhere else.
* Anything an editor user can do must also be possible through the AI control interface (`docs/AIControl.md`).
  UI actions that change the scene should call `EditorCommands` so both paths behave identically. After adding or
  changing commands run `python3 Tools/gen_command_docs.py`.
* Step-by-step checklists (new component, Lua function, editor command, shader) are in
  `.claude/skills/ember-engine-dev/SKILL.md`.
* Headless mode (`--headless`) must keep working: it is used by tests, CI and AI agents.

## Committing

1. Build with zero warnings, run `ctest` (EmberTests + feature scene) on the GPU, and `EmberTests` on llvmpipe
   when touching the renderer.
2. Review your own diff against this file: style, ownership, error handling, tests present.
3. Commit with a descriptive message (what and why). Push to `main` on GitHub.
