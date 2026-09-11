if(NOT DEFINED TOOL OR NOT DEFINED MODEL OR NOT DEFINED MODULES)
  message(FATAL_ERROR "stat test requires TOOL, MODEL, and MODULES")
endif()

execute_process(
  COMMAND "${TOOL}" query stat.summary "${MODEL}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "stat query failed (${result}):\n${output}${error}")
endif()

string(CONCAT expected
    "{\"blks\": 7, \"branches\": 1, \"callees\": 8, \"calls\": 14, "
    "\"consts\": 7, \"fns\": 3, \"loops\": 2, \"mem_elems\": 0, "
    "\"mem_slots\": 0, \"ops\": 31, \"returns\": 3, \"revision\": 0, "
    "\"static_tensor_elems\": 0, \"static_tensor_vals\": 0, "
    "\"tensor_vals\": 8, \"unresolved\": 5, \"uses\": 1, "
    "\"vals\": 38, \"yields\": 4}\n")
if(NOT output STREQUAL expected)
  message(FATAL_ERROR
          "stat query is not stable:\nexpected: ${expected}actual: ${output}")
endif()

get_filename_component(data_dir "${MODEL}" DIRECTORY)
execute_process(
  COMMAND "${TOOL}" query stat.summary "${data_dir}/stat-use.jog"
          -M "${MODULES}"
  RESULT_VARIABLE use_result
  OUTPUT_VARIABLE use_output
  ERROR_VARIABLE use_error
)
if(NOT use_result EQUAL 0)
  message(FATAL_ERROR
          "declared model dependencies were not loaded (${use_result}):\n"
          "${use_output}${use_error}")
endif()
if(NOT use_output MATCHES "\"uses\": 1")
  message(FATAL_ERROR "unexpected dependent-model summary:\n${use_output}")
endif()
