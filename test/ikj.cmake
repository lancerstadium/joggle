include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

joggle_require("IKJ extension test is missing an input" VARS TOOL CC MODEL HARNESS MODULES EXTENSIONS ROOT)

joggle_workspace("${ROOT}")

set(prepared "${ROOT}/model.jog")
set(source "${ROOT}/model.c")
set(header "${ROOT}/model.h")
set(program "${ROOT}/model")

joggle_run("IKJ preparation failed"
  COMMAND "${TOOL}" run ikj.apply c.prepare "${MODEL}"
          -M "${EXTENSIONS}" -M "${MODULES}"
  OUTPUT_FILE "${prepared}"
  ERROR_VARIABLE error)

file(READ "${prepared}" prepared_text)
if(NOT prepared_text MATCHES
   "for i in 0\\.\\.2, k in 0\\.\\.3, j in 0\\.\\.2")
  message(FATAL_ERROR "IKJ loop order is absent from the prepared model")
endif()
if(prepared_text MATCHES "tensor\\.matmul")
  message(FATAL_ERROR "IKJ preparation left the source matrix multiplication")
endif()

joggle_run("IKJ C emission failed"
  COMMAND "${TOOL}" emit c.source "${prepared}"
          -M "${EXTENSIONS}" -M "${MODULES}"
  OUTPUT_FILE "${source}"
  ERROR_VARIABLE error)

joggle_run("IKJ C header emission failed"
  COMMAND "${TOOL}" emit c.header "${prepared}"
          -M "${EXTENSIONS}" -M "${MODULES}"
  OUTPUT_FILE "${header}"
  ERROR_VARIABLE error)

file(READ "${source}" emitted_source)
file(READ "${header}" emitted_header)
if(emitted_source MATCHES
   "(^|[^A-Za-z0-9_])(joggle_|jog_|v_[A-Za-z0-9])" OR
   emitted_header MATCHES
   "(^|[^A-Za-z0-9_])(joggle_|jog_|v_[A-Za-z0-9])")
  message(FATAL_ERROR
          "IKJ C introduced a compiler-owned project prefix:\n"
          "${emitted_header}\n${emitted_source}")
endif()
if(NOT emitted_source MATCHES
   "void demo_main\\(const float\\* a, const float\\* b, float\\* out_out\\)")
  message(FATAL_ERROR
          "IKJ C did not preserve its source-derived names:\n${emitted_source}")
endif()

joggle_run("IKJ C compilation failed"
  COMMAND "${CC}" -std=c99 -Wall -Wextra -Wstrict-prototypes -Werror
          -include "${header}" "${source}" "${HARNESS}" -o "${program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

joggle_run("IKJ result is wrong"
  COMMAND "${program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
