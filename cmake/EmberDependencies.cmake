# Third-party dependencies. Every dependency is pinned to an exact tag or commit so builds are reproducible.
include(FetchContent)

set(FETCHCONTENT_QUIET ON)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# Third-party code must never be built with Ember's warnings-as-errors policy.
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)

# Helper: fetch sources only, without running the dependency's own CMakeLists (for header-only or
# sources that Ember compiles itself).
macro(ember_fetch_sources name repo tag)
	FetchContent_Declare(${name}
		GIT_REPOSITORY ${repo}
		GIT_TAG ${tag}
		GIT_SHALLOW FALSE
		SOURCE_SUBDIR __ember_no_cmake__)
	FetchContent_MakeAvailable(${name})
endmacro()

# ---------------------------------------------------------------------------------------------------
# GLFW - windowing and input
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
	GIT_REPOSITORY https://github.com/glfw/glfw.git
	GIT_TAG 3.4
	GIT_SHALLOW TRUE
	SYSTEM)
FetchContent_MakeAvailable(glfw)

# ---------------------------------------------------------------------------------------------------
# NVRHI - graphics API abstraction (Vulkan backend only)
set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX12 OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_NVAPI OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_AFTERMATH OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_RTXMU OFF CACHE BOOL "" FORCE)
set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
set(NVRHI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nvrhi
	GIT_REPOSITORY https://github.com/NVIDIA-RTX/NVRHI.git
	GIT_TAG bf0bbd9b666a7297492beb1999a4ddc0dfded143
	GIT_SUBMODULES ""
	# Fallback to non-cached host-visible memory for readback buffers (required on Raspberry Pi V3DV).
	PATCH_COMMAND "${CMAKE_COMMAND}" -DPATCH_FILE=${CMAKE_CURRENT_LIST_DIR}/patches/nvrhi-host-cached-fallback.patch
		-P "${CMAKE_CURRENT_LIST_DIR}/ApplyPatch.cmake"
	SYSTEM)
FetchContent_MakeAvailable(nvrhi)
# Vulkan-Headers is fetched by NVRHI itself; treat its headers as system headers too.
foreach(_vk_target Vulkan-Headers nvrhi nvrhi_vk)
	if(TARGET ${_vk_target})
		set_target_properties(${_vk_target} PROPERTIES SYSTEM ON)
	endif()
endforeach()

# ---------------------------------------------------------------------------------------------------
# glm - math
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glm
	GIT_REPOSITORY https://github.com/g-truc/glm.git
	GIT_TAG 1.0.1
	GIT_SHALLOW TRUE
	SYSTEM)
FetchContent_MakeAvailable(glm)

# ---------------------------------------------------------------------------------------------------
# spdlog - logging
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(spdlog
	GIT_REPOSITORY https://github.com/gabime/spdlog.git
	GIT_TAG v1.15.3
	GIT_SHALLOW TRUE
	SYSTEM)
FetchContent_MakeAvailable(spdlog)

# ---------------------------------------------------------------------------------------------------
# EnTT - entity component system
FetchContent_Declare(entt
	GIT_REPOSITORY https://github.com/skypjack/entt.git
	GIT_TAG v3.15.0
	GIT_SHALLOW TRUE
	SYSTEM)
FetchContent_MakeAvailable(entt)

# ---------------------------------------------------------------------------------------------------
# yaml-cpp - scene / project serialization
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
set(YAML_CPP_FORMAT_SOURCE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(yaml-cpp
	GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
	GIT_TAG 0.8.0
	GIT_SHALLOW TRUE
	SYSTEM)
FetchContent_MakeAvailable(yaml-cpp)

# ---------------------------------------------------------------------------------------------------
# Jolt Physics
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(JoltPhysics
	GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
	GIT_TAG v5.3.0
	GIT_SHALLOW TRUE
	SOURCE_SUBDIR Build
	SYSTEM)
FetchContent_MakeAvailable(JoltPhysics)

# ---------------------------------------------------------------------------------------------------
# Lua 5.4 - scripting (compiled as a static C library by Ember)
ember_fetch_sources(lua https://github.com/lua/lua.git v5.4.7)
file(GLOB EMBER_LUA_SOURCES "${lua_SOURCE_DIR}/*.c")
list(FILTER EMBER_LUA_SOURCES EXCLUDE REGEX ".*/(lua|luac|onelua|ltests)\\.c$")
add_library(ember_lua STATIC ${EMBER_LUA_SOURCES})
target_include_directories(ember_lua SYSTEM PUBLIC "${lua_SOURCE_DIR}")
set_target_properties(ember_lua PROPERTIES LINKER_LANGUAGE C)
if(UNIX AND NOT APPLE)
	target_compile_definitions(ember_lua PRIVATE LUA_USE_LINUX)
	target_link_libraries(ember_lua PUBLIC m dl)
elseif(APPLE)
	target_compile_definitions(ember_lua PRIVATE LUA_USE_MACOSX)
endif()

# sol2 - C++ <-> Lua bindings (header only)
ember_fetch_sources(sol2 https://github.com/ThePhD/sol2.git v3.5.0)
add_library(ember_sol2 INTERFACE)
target_include_directories(ember_sol2 SYSTEM INTERFACE "${sol2_SOURCE_DIR}/include")
target_link_libraries(ember_sol2 INTERFACE ember_lua)
target_compile_definitions(ember_sol2 INTERFACE SOL_ALL_SAFETIES_ON=1 SOL_LUA_VERSION=504)

# ---------------------------------------------------------------------------------------------------
# miniaudio - audio (single header, implementation compiled inside Ember)
ember_fetch_sources(miniaudio https://github.com/mackron/miniaudio.git 0.11.22)
add_library(ember_miniaudio INTERFACE)
target_include_directories(ember_miniaudio SYSTEM INTERFACE "${miniaudio_SOURCE_DIR}")

# ---------------------------------------------------------------------------------------------------
# cgltf (glTF loading) and stb (image loading/writing) - header only
ember_fetch_sources(cgltf https://github.com/jkuhlmann/cgltf.git v1.15)
ember_fetch_sources(stb https://github.com/nothings/stb.git 6b5799bae8a1fe3792691ef4b72ae26434c3e799)
add_library(ember_headeronly INTERFACE)
target_include_directories(ember_headeronly SYSTEM INTERFACE "${cgltf_SOURCE_DIR}" "${stb_SOURCE_DIR}")

# ---------------------------------------------------------------------------------------------------
# Dear ImGui (docking) + ImGuizmo - editor UI
ember_fetch_sources(imgui https://github.com/ocornut/imgui.git v1.91.9b-docking)
ember_fetch_sources(imguizmo https://github.com/CedricGuillemet/ImGuizmo.git 2310acda820d7383d4c4884b7945ada92cd16a47)
add_library(ember_imgui STATIC
	"${imgui_SOURCE_DIR}/imgui.cpp"
	"${imgui_SOURCE_DIR}/imgui_demo.cpp"
	"${imgui_SOURCE_DIR}/imgui_draw.cpp"
	"${imgui_SOURCE_DIR}/imgui_tables.cpp"
	"${imgui_SOURCE_DIR}/imgui_widgets.cpp"
	"${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp"
	"${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
	"${imguizmo_SOURCE_DIR}/ImGuizmo.cpp")
target_include_directories(ember_imgui SYSTEM PUBLIC
	"${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends" "${imgui_SOURCE_DIR}/misc/cpp" "${imguizmo_SOURCE_DIR}")
target_link_libraries(ember_imgui PUBLIC glfw)
target_compile_definitions(ember_imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

# ---------------------------------------------------------------------------------------------------
# doctest - unit testing (header only)
if(EMBER_BUILD_TESTS)
	ember_fetch_sources(doctest https://github.com/doctest/doctest.git v2.4.12)
	add_library(ember_doctest INTERFACE)
	target_include_directories(ember_doctest SYSTEM INTERFACE "${doctest_SOURCE_DIR}")
endif()
