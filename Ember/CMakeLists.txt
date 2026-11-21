# Ember core engine (static library)

file(GLOB_RECURSE EMBER_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

file(GLOB EMBER_SHADER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.glsl")
file(GLOB EMBER_SHADER_INCLUDES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/include/*.glsl")

add_library(Ember STATIC ${EMBER_SOURCES})
ember_set_compile_options(Ember)

target_include_directories(Ember PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_definitions(Ember PUBLIC
	GLM_FORCE_DEPTH_ZERO_TO_ONE
	GLM_FORCE_RADIANS
	GLM_ENABLE_EXPERIMENTAL
	$<$<CONFIG:Debug>:EMBER_DEBUG>
	$<$<CONFIG:Release>:EMBER_RELEASE>
	$<$<CONFIG:RelWithDebInfo>:EMBER_RELEASE>
	$<$<CONFIG:Dist>:EMBER_DIST>)

# Implementations of single-header libraries, compiled without Ember's strict warnings.
add_library(EmberVendor STATIC
	"${CMAKE_CURRENT_SOURCE_DIR}/vendor/StbImpl.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/vendor/CgltfImpl.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/vendor/MiniaudioImpl.c")
target_link_libraries(EmberVendor PUBLIC ember_headeronly ember_miniaudio)
if(UNIX AND NOT APPLE)
	find_package(Threads REQUIRED)
	target_link_libraries(EmberVendor PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

target_link_libraries(Ember PUBLIC
	nvrhi_vk
	nvrhi
	Vulkan::Headers
	glfw
	glm::glm
	spdlog::spdlog
	EnTT::EnTT
	yaml-cpp::yaml-cpp
	Jolt
	ember_sol2
	ember_imgui
	EmberVendor)

if(EMBER_SHADER_SOURCES)
	ember_compile_shaders(Ember
		OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders"
		SHADERS ${EMBER_SHADER_SOURCES})
endif()
