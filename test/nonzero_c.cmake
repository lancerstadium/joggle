if(NOT DEFINED TOOL OR NOT DEFINED CC OR NOT DEFINED MODEL OR
   NOT DEFINED HARNESS OR NOT DEFINED MODULES OR NOT DEFINED ROOT)
  message(FATAL_ERROR
          "NonZero C test requires TOOL, CC, MODEL, HARNESS, MODULES, ROOT")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")
set(prepared "${ROOT}/prepared.jog")
set(planned "${ROOT}/planned.jog")
set(source "${ROOT}/model.c")
set(program "${ROOT}/model")

execute_process(
  COMMAND "${TOOL}" run c.prepare "${MODEL}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_FILE "${prepared}" ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "NonZero C preparation failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run mem.plan "${prepared}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_FILE "${planned}" ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "NonZero C planning failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" query c.api "${planned}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_VARIABLE api ERROR_VARIABLE error
)
if(NOT result EQUAL 0 OR
   NOT api MATCHES "\\\"capacity\\\": \\[2, 6\\]" OR
   NOT api MATCHES "\\\"shape\\\": \\[2, \\\"_\\\"\\]")
  message(FATAL_ERROR
          "NonZero C API lacks its shape contract (${result}):\n${api}${error}")
endif()
execute_process(
  COMMAND "${TOOL}" query c.frontier "${planned}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_VARIABLE frontier ERROR_VARIABLE error
)
if(NOT result EQUAL 0 OR NOT frontier STREQUAL "[]\n")
  message(FATAL_ERROR
          "prepared NonZero retains a capability frontier (${result}):\n"
          "${frontier}${error}")
endif()
execute_process(
  COMMAND "${TOOL}" emit c.source "${planned}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_FILE "${source}" ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "NonZero C emission failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CC}" -std=c11 -O2 -Wall -Wextra -Werror -pedantic-errors
          "${source}" "${HARNESS}" -o "${program}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "NonZero C did not compile (${result}):\n${output}${error}")
endif()
execute_process(
  COMMAND "${program}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "NonZero C returned wrong coordinates (${result}):\n"
          "${output}${error}")
endif()
