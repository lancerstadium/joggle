include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

joggle_require("report test requires TOOL, MODEL, module roots, and outputs" VARS TOOL MODEL SOURCE_MODULES BUILD_MODULES OUT TIMING)

file(REMOVE "${OUT}")
file(REMOVE "${TIMING}")
joggle_run("report command failed"
  COMMAND "${TOOL}" run opt.fold_add_zero script.mark_add "${MODEL}"
          --report "${OUT}" --timing "${TIMING}"
          -M "${BUILD_MODULES}" -M "${SOURCE_MODULES}"
  OUTPUT_VARIABLE model
  ERROR_VARIABLE error)
if(NOT model MATCHES "return x")
  message(FATAL_ERROR "report command did not print the transformed model")
endif()
if(NOT EXISTS "${OUT}")
  message(FATAL_ERROR "report command did not create its report")
endif()
if(NOT EXISTS "${TIMING}")
  message(FATAL_ERROR "report command did not create its timing trace")
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
file(READ "${TIMING}" timing)
if(NOT timing MATCHES "\"succeeded\": true" OR
   NOT timing MATCHES "\"snapshot_ns\":" OR
   NOT timing MATCHES "\"initial_verification_ns\":" OR
   NOT timing MATCHES "\"function\": \"opt.fold_add_zero\"" OR
   NOT timing MATCHES "\"evaluation_ns\":" OR
   NOT timing MATCHES "\"verification_ns\":")
  message(FATAL_ERROR "timing trace is missing phase evidence:\n${timing}")
endif()
file(REMOVE "${OUT}")
file(REMOVE "${TIMING}")
