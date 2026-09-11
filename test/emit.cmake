if(NOT DEFINED TOOL OR NOT DEFINED MODEL OR
   NOT DEFINED SOURCE_MODULES OR NOT DEFINED BUILD_MODULES OR NOT DEFINED OUT)
  message(FATAL_ERROR
          "emit test requires TOOL, MODEL, module roots, and OUT")
endif()

execute_process(
  COMMAND "${TOOL}" emit script.emit_name "${MODEL}"
          -M "${SOURCE_MODULES}" -M "${BUILD_MODULES}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "emit command failed (${result}):\n${error}")
endif()
if(NOT output STREQUAL "matmul")
  message(FATAL_ERROR "emit did not write the string verbatim: '${output}'")
endif()

execute_process(
  COMMAND "${TOOL}" emit script.emit_wrong_type "${MODEL}"
          -M "${SOURCE_MODULES}" -M "${BUILD_MODULES}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(result EQUAL 0 OR NOT error MATCHES "must return str or bytes")
  message(FATAL_ERROR
          "emit accepted a non-artifact result (${result}):\n${output}${error}")
endif()

file(REMOVE "${OUT}")
execute_process(
  COMMAND "${TOOL}" emit script.emit_bytes "${MODEL}"
          -M "${SOURCE_MODULES}" -M "${BUILD_MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${OUT}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "binary emit failed (${result}):\n${error}")
endif()
file(READ "${OUT}" output HEX)
if(NOT output STREQUAL "00410aff")
  message(FATAL_ERROR "emit changed binary bytes: '${output}'")
endif()
file(REMOVE "${OUT}")
