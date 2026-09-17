include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

joggle_require("emit test requires TOOL, MODEL, module roots, and OUT" VARS TOOL MODEL SOURCE_MODULES BUILD_MODULES OUT)

joggle_run("emit command failed"
  COMMAND "${TOOL}" emit script.emit_name "${MODEL}"
          -M "${SOURCE_MODULES}" -M "${BUILD_MODULES}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
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
joggle_run("binary emit failed"
  COMMAND "${TOOL}" emit script.emit_bytes -
          -M "${SOURCE_MODULES}" -M "${BUILD_MODULES}"
  OUTPUT_FILE "${OUT}"
  ERROR_VARIABLE error
  INPUT_FILE "${MODEL}")
file(READ "${OUT}" output HEX)
if(NOT output STREQUAL "00410aff")
  message(FATAL_ERROR "emit changed binary bytes: '${output}'")
endif()
file(REMOVE "${OUT}")
