# CLI failures must be reported with a normal exit, not an uncaught exception.
function(expect_error)
  execute_process(COMMAND "${NATUREMOD}" ${ARGN}
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT "${status}" STREQUAL "2" OR "${error}" STREQUAL "")
    message(FATAL_ERROR "Expected exit 2 with diagnostic; got ${status}: ${output}${error}")
  endif()
endfunction()
expect_error(unknown "${EXAMPLES}")
expect_error(check "${EXAMPLES}/missing-mod-folder")
expect_error(check --catalog)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/invalid-catalog.json" "invalid JSON")
expect_error(check --catalog "${CMAKE_CURRENT_BINARY_DIR}/invalid-catalog.json" "${EXAMPLES}")
file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/invalid-catalog.json")
# The default authoring catalog is embedded: checking works outside the source root.
execute_process(COMMAND "${NATUREMOD}" check "${EXAMPLES}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" RESULT_VARIABLE status
  OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT "${status}" STREQUAL "0")
  message(FATAL_ERROR "Catalog must not depend on current directory: ${output}${error}")
endif()
