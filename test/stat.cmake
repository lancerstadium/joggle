include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

joggle_require("stat test requires TOOL, MODEL, and MODULES" VARS TOOL MODEL MODULES)

joggle_run("stat query failed"
  COMMAND "${TOOL}" query stat.summary "${MODEL}" -M "${MODULES}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

string(CONCAT expected
    "{\"blks\": 7, \"branches\": 1, \"callees\": 8, \"calls\": 14, "
    "\"consts\": 7, \"fns\": 3, \"loops\": 2, \"mem_elems\": 0, "
    "\"mem_slots\": 0, \"ops\": 31, \"returns\": 3, \"revision\": 1, "
    "\"static_tensor_elems\": 0, \"static_tensor_vals\": 0, "
    "\"tensor_vals\": 6, \"unresolved\": 5, \"uses\": 1, "
    "\"vals\": 36, \"yields\": 4}\n")
if(NOT output STREQUAL expected)
  message(FATAL_ERROR
          "stat query is not stable:\nexpected: ${expected}actual: ${output}")
endif()

get_filename_component(data_dir "${MODEL}" DIRECTORY)
joggle_run("declared model dependencies were not loaded"
  COMMAND "${TOOL}" query stat.summary "${data_dir}/stat-use.jog"
          -M "${MODULES}"
  OUTPUT_VARIABLE use_output
  ERROR_VARIABLE use_error)
if(NOT use_output MATCHES "\"uses\": 1")
  message(FATAL_ERROR "unexpected dependent-model summary:\n${use_output}")
endif()
