# Ember unit and integration tests (doctest)

file(GLOB_RECURSE EMBER_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_executable(EmberTests ${EMBER_TEST_SOURCES})
ember_set_compile_options(EmberTests)
target_link_libraries(EmberTests PRIVATE Ember EmberEditorCore ember_doctest)
target_include_directories(EmberTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(EmberTests PRIVATE
	EMBER_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
	EMBER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# GPU tests run headless; EMBER_GPU may be set to pick a specific adapter (e.g. llvmpipe on CI).
add_test(NAME EmberTests COMMAND EmberTests --reporters=console)
set_tests_properties(EmberTests PROPERTIES TIMEOUT 900)
