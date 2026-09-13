if(NOT DEFINED TOOL OR NOT DEFINED MODEL OR NOT DEFINED MODULES OR
   NOT DEFINED ROOT)
  message(FATAL_ERROR "stream test requires TOOL, MODEL, MODULES, and ROOT")
endif()

file(MAKE_DIRECTORY "${ROOT}")
set(folded "${ROOT}/folded.jog")
set(queried "${ROOT}/query.txt")
set(decoded "${ROOT}/decoded.jog")
set(invalid "${ROOT}/invalid.jog")
file(WRITE "${invalid}" "module invalid\nfn broken(\n")

execute_process(
  COMMAND "${TOOL}" read sample.read - -M "${MODULES}"
  INPUT_FILE "${MODEL}"
  OUTPUT_FILE "${decoded}"
  ERROR_VARIABLE error
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "stdin read failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" check - -M "${MODULES}"
  INPUT_FILE "${decoded}"
  ERROR_VARIABLE error
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "decoded stdin check failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" check - -M "${MODULES}"
  INPUT_FILE "${invalid}"
  OUTPUT_QUIET
  ERROR_VARIABLE error
  RESULT_VARIABLE result
)
if(result EQUAL 0 OR NOT error MATCHES "<stdin>:[0-9]+:[0-9]+: error:")
  message(FATAL_ERROR
          "stdin parse failure lost its logical source (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" run opt.fold_add_zero - -M "${MODULES}"
  INPUT_FILE "${MODEL}"
  OUTPUT_FILE "${folded}"
  ERROR_VARIABLE error
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "stdin run failed (${result}):\n${error}")
endif()
file(READ "${folded}" folded_text)
if(NOT folded_text MATCHES "return x" OR folded_text MATCHES "x \+ i32\(0\)")
  message(FATAL_ERROR "stdin run produced unexpected IR:\n${folded_text}")
endif()

execute_process(
  COMMAND "${TOOL}" query opt.untyped - -M "${MODULES}"
  INPUT_FILE "${folded}"
  OUTPUT_FILE "${queried}"
  ERROR_VARIABLE error
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "stdin query failed (${result}):\n${error}")
endif()
file(READ "${queried}" queried_text)
if(NOT queried_text STREQUAL "[]\n")
  message(FATAL_ERROR "stdin query produced unexpected result: ${queried_text}")
endif()

execute_process(
  COMMAND "${TOOL}" check - -M "${MODULES}"
  INPUT_FILE "${folded}"
  ERROR_VARIABLE error
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "stdin check failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" run opt.fold_add_zero - -M "${MODULES}"
  COMMAND "${TOOL}" query opt.untyped - -M "${MODULES}"
  INPUT_FILE "${MODEL}"
  OUTPUT_VARIABLE piped
  ERROR_VARIABLE error
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0 OR NOT piped STREQUAL "[]\n")
  message(FATAL_ERROR
          "cross-process stdin pipeline failed (${result}): ${piped}${error}")
endif()
