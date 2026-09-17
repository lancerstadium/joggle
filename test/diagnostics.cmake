include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

joggle_require("diagnostics test requires TOOL, MODEL, and MODULES" VARS TOOL MODEL MODULES)

execute_process(
  COMMAND "${TOOL}" --diagnostics jog check "${MODEL}" -M "${MODULES}"
  RESULT_VARIABLE jog_result
  OUTPUT_VARIABLE jog_output
  ERROR_VARIABLE jog_error
)
if(jog_result EQUAL 0)
  message(FATAL_ERROR "invalid model succeeded in structured mode")
endif()
foreach(fragment
    "\"severity\": \"error\""
    "\"message\": \"function 'incomplete' must end with return\""
    "\"line\": 4"
    "\"column\": 4")
  if(NOT jog_error MATCHES "${fragment}")
    message(FATAL_ERROR
            "structured diagnostic lacks ${fragment}:\n${jog_error}")
  endif()
endforeach()
if(NOT jog_output STREQUAL "")
  message(FATAL_ERROR "structured failure wrote to stdout: ${jog_output}")
endif()

execute_process(
  COMMAND "${TOOL}" --diagnostics jog module info absent -M "${MODULES}"
  RESULT_VARIABLE module_result
  OUTPUT_VARIABLE module_output
  ERROR_VARIABLE module_error
)
if(module_result EQUAL 0 OR
   NOT module_error MATCHES "\"message\": \"module not found: absent\"")
  message(FATAL_ERROR "module command lost structured diagnostics:\n${module_error}")
endif()
if(NOT module_output STREQUAL "")
  message(FATAL_ERROR "failed module inspection wrote to stdout: ${module_output}")
endif()

execute_process(
  COMMAND "${TOOL}" check "${MODEL}" -M "${MODULES}"
  RESULT_VARIABLE text_result
  OUTPUT_VARIABLE text_output
  ERROR_VARIABLE text_error
)
if(text_result EQUAL 0 OR
   NOT text_error MATCHES ":4:4: error: function 'incomplete' must end with return")
  message(FATAL_ERROR "default text diagnostic changed:\n${text_error}")
endif()
if(text_error MATCHES "^\\[")
  message(FATAL_ERROR "default diagnostics unexpectedly use structured mode")
endif()

execute_process(
  COMMAND "${TOOL}" --diagnostics jog --version
  RESULT_VARIABLE version_result
  OUTPUT_VARIABLE version_output
  ERROR_VARIABLE version_error
)
if(NOT version_result EQUAL 0 OR NOT version_output MATCHES "^joggle ")
  message(FATAL_ERROR
          "global diagnostic option displaced version handling:\n${version_error}")
endif()
