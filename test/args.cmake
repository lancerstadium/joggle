if(NOT DEFINED TOOL OR NOT DEFINED MODEL OR NOT DEFINED C_MODEL OR
   NOT DEFINED VM_MODEL OR NOT DEFINED MODULES)
  message(FATAL_ERROR "argument test requires TOOL, models, and MODULES")
endif()

execute_process(
  COMMAND "${TOOL}" query opt.count "${MODEL}"
          --arg "\"operator +\"" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0 OR NOT output STREQUAL "3\n")
  message(FATAL_ERROR
          "query did not receive its string argument (${result}):\n"
          "${output}${error}")
endif()

execute_process(
  COMMAND "${TOOL}" run opt.fix "${MODEL}"
          --arg "[\"operator +\"]" --arg "10" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0 OR NOT output MATCHES
   "fn add_zero\\(x: i32\\) -> i32 \\{\n  return x\n\\}")
  message(FATAL_ERROR
          "transform did not receive ordered list/int arguments (${result}):\n"
          "${output}${error}")
endif()

execute_process(
  COMMAND "${TOOL}" run c.place "${C_MODEL}"
          --arg "\"static\"" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0 OR NOT output MATCHES "\\[c.place: \"static\"\\]")
  message(FATAL_ERROR
          "transform did not receive its storage argument (${result}):\n"
          "${output}${error}")
endif()

execute_process(
  COMMAND "${TOOL}" emit vm.image "${VM_MODEL}"
          --arg "\"main\"" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0 OR NOT output MATCHES "^joggle-vm 3\nfn main\n" OR
   output MATCHES "fn root\n")
  message(FATAL_ERROR
          "emitter did not receive its entry argument (${result}):\n"
          "${output}${error}")
endif()

execute_process(
  COMMAND "${TOOL}" query opt.count "${MODEL}"
          --arg "{broken" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(result EQUAL 0 OR NOT error MATCHES
   "<argument>:1:[0-9]+: error: expected ':'")
  message(FATAL_ERROR
          "malformed argument was not diagnosed (${result}):\n"
          "${output}${error}")
endif()
