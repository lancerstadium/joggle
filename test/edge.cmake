include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

joggle_require("external kernel extension is missing an input" VARS TOOL CC MODEL KERNEL HARNESS MODULES EXTENSIONS ROOT)

joggle_workspace("${ROOT}")

set(selected "${ROOT}/selected.jog")
set(prepared "${ROOT}/model.jog")
set(source "${ROOT}/model.c")
set(header "${ROOT}/model.h")
set(program "${ROOT}/model")

joggle_run("external kernel selection failed"
  COMMAND "${TOOL}" run edge.apply "${MODEL}"
          -M "${EXTENSIONS}" -M "${MODULES}"
  OUTPUT_FILE "${selected}"
  ERROR_VARIABLE error)

file(READ "${selected}" selected_text)
string(REGEX MATCHALL " = conv2d\\(" selected_convs "${selected_text}")
list(LENGTH selected_convs selected_conv_count)
string(REGEX MATCHALL " = nn\\.conv2d\\(" semantic_convs "${selected_text}")
list(LENGTH semantic_convs semantic_conv_count)
if(NOT selected_conv_count EQUAL 3 OR NOT semantic_conv_count EQUAL 1)
  message(FATAL_ERROR
          "implementation guard did not preserve the other layout:\n${selected_text}")
endif()

joggle_run("external kernel preparation failed"
  COMMAND "${TOOL}" run edge.apply c.prepare "${MODEL}"
          -M "${EXTENSIONS}" -M "${MODULES}"
  OUTPUT_FILE "${prepared}"
  ERROR_VARIABLE error)

file(READ "${prepared}" prepared_text)
if(NOT prepared_text MATCHES "use edge" OR
   NOT prepared_text MATCHES "matmul\\(a, b, 2, 2, 3\\)" OR
   NOT prepared_text MATCHES
       "conv2d\\(x, weight, 1, 3, 3, 1, 1, 2, 2, 2, 2" OR
   NOT prepared_text MATCHES "edge.model.measure")
  message(FATAL_ERROR
          "external implementation selection was not preserved:\n${prepared_text}")
endif()

joggle_run("external kernel emission failed"
  COMMAND "${TOOL}" emit c.source "${prepared}"
          -M "${EXTENSIONS}" -M "${MODULES}"
  OUTPUT_FILE "${source}"
  ERROR_VARIABLE error)

file(READ "${source}" emitted)
if(emitted MATCHES "(^|[^A-Za-z0-9_])(joggle_|jog_|v_[A-Za-z0-9])")
  message(FATAL_ERROR
          "external-kernel C introduced a compiler-owned project prefix:\n"
          "${emitted}")
endif()
if(NOT emitted MATCHES
   "data_weight\\[16\\] = \\{0x00, 0x00, 0x80, 0x3f")
  message(FATAL_ERROR
          "external-kernel constant is not a byte array:\n${emitted}")
endif()
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
   "edge_matmul\\(a, b, 2, 2, 3, product_out\\);")
  message(FATAL_ERROR "external kernel call is absent:\n${emitted}")
endif()
if(NOT emitted MATCHES
   "edge_matmul\\(a, b, 1, 4, 3, product_out\\);")
  message(FATAL_ERROR "second external kernel shape is absent:\n${emitted}")
endif()
if(NOT emitted MATCHES
   "edge_conv2d\\(x, weight, 1, 3, 3, 1, 1, 2, 2, 2, 2")
  message(FATAL_ERROR "external convolution call is absent:\n${emitted}")
endif()
if(NOT emitted MATCHES
   "edge_conv2d\\(x, weight, 1, 3, 3, 1, 1, 2, 2, 2, 2[^;]*, convolved(_out|_[0-9]+)\\);" OR
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

joggle_run("external kernel header failed"
  COMMAND "${TOOL}" emit c.header "${prepared}"
          -M "${EXTENSIONS}" -M "${MODULES}"
  OUTPUT_FILE "${header}"
  ERROR_VARIABLE error)

joggle_run("external kernel did not compile"
  COMMAND "${CC}" -std=c99 -Wall -Wextra -Wstrict-prototypes -Werror
          -include "${header}" "${source}" "${KERNEL}" "${HARNESS}"
          -o "${program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

joggle_run("external kernel returned the wrong result"
  COMMAND "${program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
