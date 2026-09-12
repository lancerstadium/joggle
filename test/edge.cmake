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
  COMMAND "${TOOL}" run edge.apply c.prepare "${MODEL}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${prepared}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "external kernel preparation failed (${result}):\n${error}")
endif()

file(READ "${prepared}" prepared_text)
if(NOT prepared_text MATCHES "use edge" OR
   NOT prepared_text MATCHES "matmul\\(a, b, 2, 2, 3\\)" OR
   NOT prepared_text MATCHES
       "conv2d\\(x, weight, 1, 3, 3, 1, 1, 2, 2, 2, 2" OR
   NOT prepared_text MATCHES "edge.model.measure")
  message(FATAL_ERROR
          "external implementation selection was not preserved:\n${prepared_text}")
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
   "void edge_matmul\\(const float\\* a, const float\\* b, int64_t rows, int64_t columns, int64_t inner, float\\* out\\);")
  message(FATAL_ERROR "external kernel prototype is absent:\n${emitted}")
endif()
string(REGEX MATCHALL "void edge_matmul\\(" matmul_prototypes "${emitted}")
list(LENGTH matmul_prototypes matmul_prototype_count)
if(NOT matmul_prototype_count EQUAL 1)
  message(FATAL_ERROR
          "external kernel prototype was not deduplicated:\n${emitted}")
endif()
if(NOT emitted MATCHES
   "edge_matmul\\(a, b, 2, 2, 3, product\\);")
  message(FATAL_ERROR "external kernel call is absent:\n${emitted}")
endif()
if(NOT emitted MATCHES
   "edge_matmul\\(a, b, 1, 4, 3, product\\);")
  message(FATAL_ERROR "second external kernel shape is absent:\n${emitted}")
endif()
if(NOT emitted MATCHES
   "edge_conv2d\\(x, weight, 1, 3, 3, 1, 1, 2, 2, 2, 2")
  message(FATAL_ERROR "external convolution call is absent:\n${emitted}")
endif()
if(NOT emitted MATCHES
   "edge_conv2d\\(x, weight, 1, 3, 3, 1, 1, 2, 2, 2, 2[^;]*, convolved\\);" OR
   emitted MATCHES "edge_conv2d\\([^;]*hex\"")
  message(FATAL_ERROR
          "tensor constant is not named at the external boundary:\n${emitted}")
endif()
if(NOT emitted MATCHES
   "void edge_extrema\\(const float\\* x, float\\* out_0, float\\* out_1\\);" OR
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
