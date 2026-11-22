# Binary resources (fonts, ...) embedded into the engine so runtime and exported games need no extra files.

# ember_embed_resources(<target> OUTPUT_DIR <dir> RESOURCES <Name> <path> [<Name> <path>...])
# Each resource becomes Ember::EmbeddedResources::g_<Name>, looked up with EmbeddedResources::Find("<Name>").
function(ember_embed_resources target)
	cmake_parse_arguments(ARG "" "OUTPUT_DIR" "RESOURCES" ${ARGN})

	list(LENGTH ARG_RESOURCES resource_count)
	math(EXPR odd "${resource_count} % 2")
	if(resource_count EQUAL 0 OR odd)
		message(FATAL_ERROR "ember_embed_resources: RESOURCES must be a list of <Name> <path> pairs")
	endif()

	set(files "")
	math(EXPR last_index "${resource_count} - 1")
	foreach(index RANGE 1 ${last_index} 2)
		list(GET ARG_RESOURCES ${index} path)
		if(NOT EXISTS "${path}")
			message(FATAL_ERROR "ember_embed_resources: resource file '${path}' does not exist")
		endif()
		list(APPEND files "${path}")
	endforeach()

	set(header "${ARG_OUTPUT_DIR}/EmberResources.h")
	set(source "${ARG_OUTPUT_DIR}/EmberResources.cpp")
	add_custom_command(
		OUTPUT "${header}" "${source}"
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARG_OUTPUT_DIR}"
		COMMAND "${CMAKE_COMMAND}" -DOUTPUT_HEADER=${header} -DOUTPUT_SOURCE=${source}
			"-DRESOURCES=${ARG_RESOURCES}" -P "${CMAKE_SOURCE_DIR}/cmake/EmbedResources.cmake"
		DEPENDS ${files} "${CMAKE_SOURCE_DIR}/cmake/EmbedResources.cmake"
		COMMENT "Embedding binary resources"
		VERBATIM)

	target_sources(${target} PRIVATE "${header}" "${source}")
	target_include_directories(${target} PUBLIC "${ARG_OUTPUT_DIR}")
endfunction()
