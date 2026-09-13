if(NOT DEFINED TOOL OR NOT DEFINED MODEL OR NOT DEFINED MODULES)
  message(FATAL_ERROR "opt test requires TOOL, MODEL, and MODULES")
endif()

execute_process(
  COMMAND "${TOOL}" run opt.fold_add_zero opt.basic "${MODEL}"
          -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "overload-safe optimization failed (${result}):\n${error}")
endif()
string(FIND "${output}" "return x + i32(0)" used)
string(FIND "${output}" "let observed = x + i32(1)" unused)
if(used EQUAL -1 OR unused EQUAL -1)
  message(FATAL_ERROR
          "built-in algebra was applied to a user overload:\n${output}")
endif()
