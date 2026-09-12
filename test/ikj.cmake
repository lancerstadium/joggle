if(NOT DEFINED TOOL OR NOT DEFINED CC OR NOT DEFINED MODEL OR
   NOT DEFINED HARNESS OR NOT DEFINED MODULES OR NOT DEFINED EXAMPLES OR
   NOT DEFINED ROOT)
  message(FATAL_ERROR "IKJ example test is missing an input")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")

set(prepared "${ROOT}/model.jog")
set(source "${ROOT}/model.c")
set(header "${ROOT}/model.h")
set(program "${ROOT}/model")

execute_process(
  COMMAND "${TOOL}" run ikj.apply c.prepare "${MODEL}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${prepared}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "IKJ preparation failed (${result}):\n${error}")
endif()

file(READ "${prepared}" prepared_text)
if(NOT prepared_text MATCHES
   "for i in 0\\.\\.2, k in 0\\.\\.3, j in 0\\.\\.2")
  message(FATAL_ERROR "IKJ loop order is absent from the prepared model")
endif()
if(prepared_text MATCHES "tensor\\.matmul")
  message(FATAL_ERROR "IKJ preparation left the source matrix multiplication")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.source "${prepared}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "IKJ C emission failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.header "${prepared}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${header}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "IKJ C header emission failed (${result}):\n${error}")
endif()

file(READ "${source}" emitted_source)
file(READ "${header}" emitted_header)
if(emitted_source MATCHES "(^|[^A-Za-z0-9_])(jog_|v_[A-Za-z0-9])" OR
   emitted_header MATCHES "(^|[^A-Za-z0-9_])(jog_|v_[A-Za-z0-9])")
  message(FATAL_ERROR
          "IKJ C introduced a compiler-owned project prefix:\n"
          "${emitted_header}\n${emitted_source}")
endif()
if(NOT emitted_source MATCHES
   "void demo_main\\(const float\\* a, const float\\* b, float\\* out_out\\)")
  message(FATAL_ERROR
          "IKJ C did not preserve its source-derived names:\n${emitted_source}")
endif()

execute_process(
  COMMAND "${CC}" -std=c99 -Wall -Wextra -Wstrict-prototypes -Werror
          -include "${header}" "${source}" "${HARNESS}" -o "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "IKJ C compilation failed (${result}):\n${output}${error}")
endif()

execute_process(
  COMMAND "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "IKJ result is wrong (${result}):\n${output}${error}")
endif()
