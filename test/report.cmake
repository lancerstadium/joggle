if(NOT DEFINED TOOL OR NOT DEFINED MODEL OR NOT DEFINED MODULES OR
   NOT DEFINED OUT)
  message(FATAL_ERROR "report test requires TOOL, MODEL, MODULES, and OUT")
endif()

file(REMOVE "${OUT}")
execute_process(
  COMMAND "${TOOL}" run opt.fold_add_zero "${MODEL}"
          --report "${OUT}" -M "${MODULES}"
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
   NOT report MATCHES "\"fn\": \"opt.fold_identity\"")
  message(FATAL_ERROR "report is missing execution evidence:\n${report}")
endif()
file(REMOVE "${OUT}")
