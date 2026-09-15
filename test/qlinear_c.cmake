if(NOT DEFINED TOOL OR NOT DEFINED CC OR NOT DEFINED MODEL OR
   NOT DEFINED HARNESS OR NOT DEFINED MODULES OR NOT DEFINED ROOT)
  message(FATAL_ERROR
          "QLinear C test requires TOOL, CC, MODEL, HARNESS, MODULES, ROOT")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")
set(converted "${ROOT}/converted.jog")
set(prepared "${ROOT}/prepared.jog")
set(planned "${ROOT}/planned.jog")
set(source "${ROOT}/model.c")
set(program "${ROOT}/model")

execute_process(
  COMMAND "${TOOL}" run onnx.nn.infer onnx.nn.convert "${MODEL}"
          -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_FILE "${converted}" ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "QLinear conversion failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run c.prepare "${converted}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_FILE "${prepared}" ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "QLinear preparation failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run mem.plan "${prepared}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_FILE "${planned}" ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "QLinear planning failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" query c.frontier "${planned}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_VARIABLE frontier ERROR_VARIABLE error
)
if(NOT result EQUAL 0 OR NOT frontier STREQUAL "[]\n")
  message(FATAL_ERROR
          "prepared QLinear retains a C frontier (${result}):\n${frontier}${error}")
endif()
execute_process(
  COMMAND "${TOOL}" emit c.source "${planned}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_FILE "${source}" ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "QLinear emission failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CC}" -std=c11 -O2 -Wall -Wextra -Werror -pedantic-errors
          "${source}" "${HARNESS}" -lm -o "${program}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "QLinear C did not compile (${result}):\n${output}${error}")
endif()
execute_process(
  COMMAND "${program}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "QLinear C returned wrong values (${result}):\n${output}${error}")
endif()
