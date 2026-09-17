include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

joggle_require("report test requires TOOL, MODEL, module roots, and OUT" VARS TOOL MODEL SOURCE_MODULES BUILD_MODULES OUT)

file(REMOVE "${OUT}")
joggle_run("report command failed"
  COMMAND "${TOOL}" run opt.fold_add_zero script.mark_add "${MODEL}"
          --report "${OUT}" -M "${BUILD_MODULES}" -M "${SOURCE_MODULES}"
  OUTPUT_VARIABLE model
  ERROR_VARIABLE error)
if(NOT model MATCHES "return x")
  message(FATAL_ERROR "report command did not print the transformed model")
endif()
if(NOT EXISTS "${OUT}")
  message(FATAL_ERROR "report command did not create its report")
endif()
file(READ "${OUT}" report)
if(NOT report MATCHES "\"changed\": true" OR
   NOT report MATCHES "\"calls\":" OR
   NOT report MATCHES "\"cached\":" OR
   NOT report MATCHES "\"kind\": \"fn\"" OR
   NOT report MATCHES "\"fn\": \"opt.fold_add_zero\"" OR
   NOT report MATCHES "\"fn\": \"opt.fold_identity\"" OR
   NOT report MATCHES "\"fn\": \"script.mark_add\"")
  message(FATAL_ERROR "report is missing execution evidence:\n${report}")
endif()
file(REMOVE "${OUT}")
