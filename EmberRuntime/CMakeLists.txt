# Ember runtime: plays an exported game (no editor tooling).

file(GLOB_RECURSE EMBER_RUNTIME_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_executable(EmberRuntime ${EMBER_RUNTIME_SOURCES})
ember_set_compile_options(EmberRuntime)
target_link_libraries(EmberRuntime PRIVATE Ember)
