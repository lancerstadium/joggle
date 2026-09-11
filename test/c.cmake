if(NOT DEFINED TOOL OR NOT DEFINED CC OR NOT DEFINED MODEL OR
   NOT DEFINED OPEN_MODEL OR NOT DEFINED COLLISION_MODEL OR
   NOT DEFINED HARNESS OR NOT DEFINED OPEN_HARNESS OR
   NOT DEFINED MODULES OR NOT DEFINED ROOT)
  message(FATAL_ERROR
          "C execution test requires TOOL, CC, MODEL, HARNESS, MODULES, ROOT")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")

execute_process(
  COMMAND "${TOOL}" emit c.source "${COLLISION_MODEL}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(result EQUAL 0 OR NOT error MATCHES "overloaded or colliding function name")
  message(FATAL_ERROR
          "C emission accepted a symbol collision (${result}):\n"
          "${output}${error}")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.source "${OPEN_MODEL}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(result EQUAL 0 OR NOT error MATCHES "must be prepared")
  message(FATAL_ERROR
          "C emission accepted an unexposed dependency call (${result}):\n"
          "${output}${error}")
endif()

set(prepared "${ROOT}/prepared.jog")
set(prepared_again "${ROOT}/prepared-again.jog")
set(open_source "${ROOT}/open.c")
set(open_header "${ROOT}/open.h")
set(open_program "${ROOT}/open-model")
execute_process(
  COMMAND "${TOOL}" run c.prepare "${OPEN_MODEL}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${prepared}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "C preparation failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run c.prepare "${prepared}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${prepared_again}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "repeated C preparation failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${prepared}" "${prepared_again}"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "C preparation is not idempotent")
endif()
execute_process(
  COMMAND "${TOOL}" emit c.source "${prepared}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${open_source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "prepared C emission failed (${result}):\n${error}")
endif()
file(READ "${open_source}" emitted)
if(emitted MATCHES "jog_math")
  message(FATAL_ERROR
          "prepared C declared a math function already emitted through libm:\n"
          "${emitted}")
endif()
if(NOT emitted MATCHES "static int64_t jog_offset\\(int64_t v_x\\)")
  message(FATAL_ERROR
          "prepared C did not give a local helper internal linkage:\n${emitted}")
endif()
execute_process(
  COMMAND "${TOOL}" emit c.header "${prepared}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${open_header}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "prepared C header emission failed (${result}):\n${error}")
endif()
file(READ "${open_header}" emitted_header)
if(emitted_header MATCHES "jog_offset")
  message(FATAL_ERROR
          "prepared C header exposed an unmarked helper:\n${emitted_header}")
endif()
if(NOT emitted_header MATCHES "jog_carry" OR
   NOT emitted_header MATCHES "jog_add" OR
   NOT emitted_header MATCHES "jog_sigmoid")
  message(FATAL_ERROR
          "prepared C header omitted a marked entry:\n${emitted_header}")
endif()
execute_process(
  COMMAND "${CC}" -std=c99 -Wall -Wextra -Wstrict-prototypes -Werror
          -include "${open_header}"
          "${open_source}" "${OPEN_HARNESS}" -lm -o "${open_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  file(READ "${open_source}" emitted)
  message(FATAL_ERROR
          "prepared C did not compile (${result}):\n${output}${error}\n${emitted}")
endif()
execute_process(
  COMMAND "${open_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "prepared C returned the wrong result (${result}):\n${output}${error}")
endif()

set(source "${ROOT}/model.c")
set(header "${ROOT}/model.h")
set(program "${ROOT}/model")

execute_process(
  COMMAND "${TOOL}" emit c.source "${MODEL}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "C emission failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.header "${MODEL}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${header}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "C header emission failed (${result}):\n${error}")
endif()
file(READ "${header}" emitted_header)
if(NOT emitted_header MATCHES
   "int64_t jog_abi_probe\\(int64_t v_i, int64_t v_n, int32_t v_x\\);")
  message(FATAL_ERROR
          "C header did not apply its scalar ABI structurally:\n${emitted_header}")
endif()
if(emitted_header MATCHES "size_t")
  message(FATAL_ERROR
          "C header leaked an emitter-private array counter:\n${emitted_header}")
endif()
file(READ "${source}" emitted_source)
if(NOT emitted_source MATCHES "for \\(size_t jog_i = 0;")
  message(FATAL_ERROR
          "C source did not use an unsigned host count for fixed storage:\n"
          "${emitted_source}")
endif()
string(FIND "${emitted_source}"
       "bool jog_logical(int64_t v_a, int64_t v_b) {\n  if"
       duplicate_logical)
if(NOT duplicate_logical EQUAL -1)
  message(FATAL_ERROR
          "C source emitted a short-circuit expression twice:\n"
          "${emitted_source}")
endif()

execute_process(
  COMMAND "${CC}" -std=c99 -Wall -Wextra -Wstrict-prototypes -Werror
          -include "${header}" "${source}" "${HARNESS}" -lm -o "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  file(READ "${source}" emitted)
  message(FATAL_ERROR
          "generated C did not compile (${result}):\n${output}${error}\n${emitted}")
endif()

execute_process(
  COMMAND "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "generated C returned the wrong result (${result}):\n${output}${error}")
endif()
