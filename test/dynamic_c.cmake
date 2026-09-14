if(NOT DEFINED TOOL OR NOT DEFINED CC OR NOT DEFINED MODEL OR
   NOT DEFINED HARNESS OR NOT DEFINED MODULES OR NOT DEFINED ROOT)
  message(FATAL_ERROR
          "dynamic C test requires TOOL, CC, MODEL, HARNESS, MODULES, ROOT")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")
set(prepared "${ROOT}/prepared.jog")
set(planned "${ROOT}/planned.jog")
set(source "${ROOT}/model.c")
set(header "${ROOT}/model.h")
set(program "${ROOT}/model")

execute_process(
  COMMAND "${TOOL}" run c.prepare "${MODEL}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_FILE "${prepared}" ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "dynamic C preparation failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run mem.plan "${prepared}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_FILE "${planned}" ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "dynamic C planning failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" emit c.source "${planned}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_FILE "${source}" ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "dynamic C emission failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" emit c.header "${planned}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_FILE "${header}" ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "dynamic C header failed (${result}):\n${error}")
endif()
file(READ "${header}" header_text)
if(NOT header_text MATCHES
   "void dynamic_c_prefix\\(int32_t\\* out, int64_t\\* out_dim_0\\);" OR
   NOT header_text MATCHES
   "int32_t dynamic_c_sum\\(const int32_t\\* x, int64_t x_dim_0\\);")
  message(FATAL_ERROR "dynamic C header disagrees with the source:\n${header_text}")
endif()
execute_process(
  COMMAND "${TOOL}" query c.api "${planned}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_VARIABLE api ERROR_VARIABLE error
)
if(NOT result EQUAL 0 OR
   NOT api MATCHES "\\\"capacity\\\": \\[4, 2\\]" OR
   NOT api MATCHES "\\\"shape\\\": \\[\\\"_\\\", 2\\]" OR
   NOT api MATCHES "x_dim_0")
  message(FATAL_ERROR
          "dynamic C API metadata is incomplete (${result}):\n${api}${error}")
endif()
execute_process(
  COMMAND "${TOOL}" query c.frontier "${planned}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_VARIABLE frontier ERROR_VARIABLE error
)
if(NOT result EQUAL 0 OR NOT frontier STREQUAL "[]\n")
  message(FATAL_ERROR
          "prepared dynamic C retains a capability frontier (${result}):\n"
          "${frontier}${error}")
endif()
file(READ "${source}" emitted)
if(NOT emitted MATCHES
   "void dynamic_c_prefix\\(int32_t\\* out, int64_t\\* out_dim_0\\)")
  message(FATAL_ERROR "dynamic C ABI is not flat and explicit:\n${emitted}")
endif()
if(NOT emitted MATCHES
   "int32_t dynamic_c_sum\\(const int32_t\\* x, int64_t x_dim_0\\)")
  message(FATAL_ERROR "dynamic input extents are not explicit:\n${emitted}")
endif()
execute_process(
  COMMAND "${CC}" -std=c11 -Wall -Wextra -Werror -pedantic-errors
          "${source}" "${HARNESS}" -o "${program}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "dynamic C did not compile (${result}):\n${output}${error}\n${emitted}")
endif()
execute_process(
  COMMAND "${program}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "dynamic C returned the wrong result (${result}):\n${output}${error}")
endif()
