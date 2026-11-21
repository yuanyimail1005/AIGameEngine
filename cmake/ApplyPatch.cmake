# Script mode: idempotently applies PATCH_FILE to the current working directory (a git checkout).
execute_process(COMMAND git apply --reverse --check "${PATCH_FILE}" RESULT_VARIABLE already_applied
	OUTPUT_QUIET ERROR_QUIET)
if(already_applied EQUAL 0)
	message(STATUS "Patch already applied: ${PATCH_FILE}")
	return()
endif()
execute_process(COMMAND git apply "${PATCH_FILE}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
	message(FATAL_ERROR "Failed to apply patch ${PATCH_FILE}")
endif()
