# Compile options applied to first-party Ember targets only (never to third-party code).
function(ember_set_compile_options target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:preprocessor)
		if(EMBER_WARNINGS_AS_ERRORS)
			target_compile_options(${target} PRIVATE /WX)
		endif()
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
			-Wno-missing-field-initializers)
		if(EMBER_WARNINGS_AS_ERRORS)
			target_compile_options(${target} PRIVATE -Werror)
		endif()
	endif()

	target_compile_definitions(${target} PRIVATE
		$<$<CONFIG:Debug>:EMBER_DEBUG>
		$<$<CONFIG:Release>:EMBER_RELEASE>
		$<$<CONFIG:RelWithDebInfo>:EMBER_RELEASE>
		$<$<CONFIG:Dist>:EMBER_DIST>)
endfunction()
