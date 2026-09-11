if(NOT DEFINED TOOL OR NOT DEFINED MODEL OR NOT DEFINED SOURCE_MODULES OR
   NOT DEFINED BUILD_MODULES OR NOT DEFINED OUT)
  message(FATAL_ERROR
          "report test requires TOOL, MODEL, module roots, and OUT")
endif()

file(REMOVE "${OUT}")
execute_process(
  COMMAND "${TOOL}" run opt.fold_add_zero script.mark_add "${MODEL}"
          --report "${OUT}" -M "${BUILD_MODULES}" -M "${SOURCE_MODULES}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE model
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "report command failed (${result}):\n${error}")
endif()
if(NOT model MATCHES "return x")
  message(FATAL_ERROR "report command did not print the transformed model")
endif()
if(NOT EXISTS "${OUT}")
  message(FATAL_ERROR "report command did not create its report")
endif()
file(READ "${OUT}" report)
if(NOT report MATCHES "\"changed\": true" OR
   NOT report MATCHES "\"kind\": \"fn\"" OR
   NOT report MATCHES "\"fn\": \"opt.fold_add_zero\"" OR
   NOT report MATCHES "\"fn\": \"opt.fold_identity\"" OR
   NOT report MATCHES "\"fn\": \"script.mark_add\"")
  message(FATAL_ERROR "report is missing execution evidence:\n${report}")
endif()
file(REMOVE "${OUT}")
