if(NOT DEFINED TOOL OR NOT DEFINED CC OR NOT DEFINED MODEL OR
   NOT DEFINED KERNEL OR NOT DEFINED HARNESS OR NOT DEFINED MODULES OR
   NOT DEFINED EXAMPLES OR NOT DEFINED ROOT)
  message(FATAL_ERROR "external kernel example is missing an input")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")

set(prepared "${ROOT}/model.jog")
set(source "${ROOT}/model.c")
set(header "${ROOT}/model.h")
set(program "${ROOT}/model")

execute_process(
  COMMAND "${TOOL}" run c.prepare "${MODEL}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${prepared}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "external kernel preparation failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.source "${prepared}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "external kernel emission failed (${result}):\n${error}")
endif()

file(READ "${source}" emitted)
if(NOT emitted MATCHES
   "void edge_matmul\\(const float\\* a, const float\\* b, float\\* joggle_result\\);")
  message(FATAL_ERROR "external kernel prototype is absent:\n${emitted}")
endif()
if(NOT emitted MATCHES
   "edge_matmul\\(a, b, joggle_tmp_[0-9]+\\);")
  message(FATAL_ERROR "external kernel call is absent:\n${emitted}")
endif()
if(NOT emitted MATCHES
   "void edge_extrema\\(const float\\* x, float\\* joggle_result_0, float\\* joggle_result_1\\);" OR
   NOT emitted MATCHES "edge_extrema\\(x, &low, &high\\);")
  message(FATAL_ERROR
          "external multi-result kernel boundary is absent:\n${emitted}")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.header "${prepared}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${header}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "external kernel header failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${CC}" -std=c99 -Wall -Wextra -Wstrict-prototypes -Werror
          -include "${header}" "${source}" "${KERNEL}" "${HARNESS}"
          -o "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "external kernel did not compile (${result}):\n${output}${error}")
endif()

execute_process(
  COMMAND "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "external kernel returned the wrong result (${result}):\n"
          "${output}${error}")
endif()
