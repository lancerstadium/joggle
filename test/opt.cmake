if(NOT DEFINED TOOL OR NOT DEFINED MODEL OR NOT DEFINED CSE_MODEL OR
   NOT DEFINED MODULES)
  message(FATAL_ERROR "opt test requires TOOL, MODEL, CSE_MODEL, and MODULES")
endif()

execute_process(
  COMMAND "${TOOL}" run opt.basic "${CSE_MODEL}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "indexed CSE failed (${result}):\n${error}")
endif()
string(REGEX MATCHALL "x \\+ i32\\(1\\)" additions "${output}")
list(LENGTH additions addition_count)
string(REGEX MATCHALL "x \\+ one" shared_additions "${output}")
list(LENGTH shared_additions shared_count)
if(NOT addition_count EQUAL 2 OR NOT shared_count EQUAL 1 OR
   output MATCHES "let second =")
  message(FATAL_ERROR
          "CSE crossed a block or retained a same-block duplicate:\n${output}")
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
