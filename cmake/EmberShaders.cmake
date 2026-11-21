# Offline GLSL -> SPIR-V compilation. Shaders are compiled at build time with glslangValidator and
# embedded into the engine binary as byte arrays, so runtime and exported games need no shader files.
find_program(EMBER_GLSLANG glslangValidator REQUIRED)

# ember_compile_shaders(<target> OUTPUT_HEADER <path> SHADERS <file>...)
# Each shader <name>.<stage>.glsl becomes a symbol g_Shader_<name>_<stage> in the generated header.
function(ember_compile_shaders target)
	cmake_parse_arguments(ARG "" "OUTPUT_DIR" "SHADERS" ${ARGN})

	set(spv_files "")
	foreach(shader ${ARG_SHADERS})
		get_filename_component(shader_name "${shader}" NAME)
		string(REGEX MATCH "^([A-Za-z0-9_]+)\\.(vert|frag|comp)\\.glsl$" _match "${shader_name}")
		if(NOT _match)
			message(FATAL_ERROR "Shader '${shader_name}' must be named <Name>.<vert|frag|comp>.glsl")
		endif()
		set(base "${CMAKE_MATCH_1}")
		set(stage "${CMAKE_MATCH_2}")
		set(spv "${ARG_OUTPUT_DIR}/${base}_${stage}.spv")
		add_custom_command(
			OUTPUT "${spv}"
			COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARG_OUTPUT_DIR}"
			COMMAND "${EMBER_GLSLANG}" -V --target-env vulkan1.2 -S ${stage}
				"-I${CMAKE_SOURCE_DIR}/Ember/shaders/include" -o "${spv}" "${shader}"
			DEPENDS "${shader}" ${EMBER_SHADER_INCLUDES}
			COMMENT "Compiling shader ${shader_name}"
			VERBATIM)
		list(APPEND spv_files "${spv}")
	endforeach()

	set(header "${ARG_OUTPUT_DIR}/EmberShaders.h")
	set(source "${ARG_OUTPUT_DIR}/EmberShaders.cpp")
	add_custom_command(
		OUTPUT "${header}" "${source}"
		COMMAND "${CMAKE_COMMAND}" -DOUTPUT_HEADER=${header} -DOUTPUT_SOURCE=${source}
			"-DSPV_FILES=${spv_files}" -P "${CMAKE_SOURCE_DIR}/cmake/EmbedShaders.cmake"
		DEPENDS ${spv_files} "${CMAKE_SOURCE_DIR}/cmake/EmbedShaders.cmake"
		COMMENT "Embedding SPIR-V shaders"
		VERBATIM)

	target_sources(${target} PRIVATE "${header}" "${source}")
	target_include_directories(${target} PUBLIC "${ARG_OUTPUT_DIR}")
endfunction()
