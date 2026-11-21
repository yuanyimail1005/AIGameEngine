# Editor core (UI-independent: editor state, command API, AI server, exporter) - also linked by the tests.
file(GLOB EMBER_EDITOR_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Core/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Core/*.h")
add_library(EmberEditorCore STATIC ${EMBER_EDITOR_CORE_SOURCES})
ember_set_compile_options(EmberEditorCore)
target_include_directories(EmberEditorCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Core")
target_link_libraries(EmberEditorCore PUBLIC Ember ember_json)
if(WIN32)
	target_link_libraries(EmberEditorCore PUBLIC ws2_32)
endif()

# Editor executable (ImGui UI)
file(GLOB EMBER_EDITOR_APP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/App/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/App/*.h")
add_executable(EmberEditor ${EMBER_EDITOR_APP_SOURCES})
ember_set_compile_options(EmberEditor)
target_link_libraries(EmberEditor PRIVATE EmberEditorCore)
# The editor exports games by copying the runtime binary that sits next to it.
add_dependencies(EmberEditor EmberRuntime)
