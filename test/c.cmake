if(NOT DEFINED TOOL OR NOT DEFINED CC OR NOT DEFINED MODEL OR
   NOT DEFINED INVALID_MODEL OR
   NOT DEFINED OPEN_MODEL OR NOT DEFINED COLLISION_MODEL OR
   NOT DEFINED HARNESS OR NOT DEFINED BLOB_HARNESS OR
   NOT DEFINED OPEN_HARNESS OR
   NOT DEFINED MODULES OR NOT DEFINED ROOT)
  message(FATAL_ERROR
          "C execution test requires TOOL, CC, models, harnesses, MODULES, ROOT")
endif()

execute_process(
  COMMAND "${TOOL}" run c.prepare "${INVALID_MODEL}" -M "${MODULES}"
  RESULT_VARIABLE invalid_result
  OUTPUT_VARIABLE invalid_output
  ERROR_VARIABLE invalid_error
)
if(invalid_result EQUAL 0 OR
   NOT invalid_error MATCHES "c: unsupported call remains: operator %")
  message(FATAL_ERROR
          "C preparation accepted an invalid floating operator "
          "(${invalid_result}):\n${invalid_output}${invalid_error}")
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
if(emitted MATCHES "math_")
  message(FATAL_ERROR
          "prepared C declared a math function already emitted through libm:\n"
          "${emitted}")
endif()
if(NOT emitted MATCHES "static int64_t open_offset\\(int64_t x\\)")
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
if(emitted_header MATCHES "open_offset")
  message(FATAL_ERROR
          "prepared C header exposed an unmarked helper:\n${emitted_header}")
endif()
if(NOT emitted_header MATCHES "open_carry" OR
   NOT emitted_header MATCHES "open_add" OR
   NOT emitted_header MATCHES "open_sigmoid")
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
set(blob_source "${ROOT}/model-blob.c")
set(blob_header "${ROOT}/model-blob.h")
set(blob_data "${ROOT}/model.bin")
set(blob_program "${ROOT}/model-blob")
set(model_prepared "${ROOT}/model.jog")
string(CONCAT abi32
    "{index: {name: \"int32_t\", bytes: 4, kind: \"signed\", include: \"stdint.h\"},"
    " int: {name: \"int32_t\", bytes: 4, kind: \"signed\", include: \"stdint.h\"}}")
set(model32_prepared "${ROOT}/model32.jog")
set(source32 "${ROOT}/model32.c")
set(header32 "${ROOT}/model32.h")
set(program32 "${ROOT}/model32")

execute_process(
  COMMAND "${TOOL}" run c.prepare "${MODEL}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${model_prepared}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "C model preparation failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" run c.prepare "${MODEL}"
          --arg "${abi32}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${model32_prepared}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "32-bit ABI preparation failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.source "${model32_prepared}"
          --arg "${abi32}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${source32}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "32-bit ABI source emission failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.header "${model32_prepared}"
          --arg "${abi32}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${header32}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "32-bit ABI header emission failed (${result}):\n${error}")
endif()
file(READ "${source32}" emitted_source32)
file(READ "${header32}" emitted_header32)
if(NOT emitted_source32 MATCHES "for \\(int32_t joggle_i = 0;" OR
   NOT emitted_header32 MATCHES
       "kernel_abi_probe\\(int32_t i, int32_t n, int32_t x\\);")
  message(FATAL_ERROR
          "C ABI override did not drive definitions and declarations:\n"
          "${emitted_header32}\n${emitted_source32}")
endif()
execute_process(
  COMMAND "${CC}" -std=c99 -Wall -Wextra -Wstrict-prototypes -Werror
          -include "${header32}" "${source32}" "${HARNESS}" -lm
          -o "${program32}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "32-bit ABI C did not compile (${result}):\n${output}${error}")
endif()
execute_process(
  COMMAND "${program32}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "32-bit ABI C returned the wrong result (${result}):\n"
          "${output}${error}")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.source "${model_prepared}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "C emission failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.header "${model_prepared}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${header}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "C header emission failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.data "${model_prepared}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${blob_data}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "C data emission failed (${result}):\n${error}")
endif()
file(READ "${blob_data}" emitted_data HEX)
if(NOT emitted_data STREQUAL "ff007f000000803f00000040")
  message(FATAL_ERROR "C data emission changed literal bytes: ${emitted_data}")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.source "${model_prepared}"
          --arg "\"model\"" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${blob_source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "external-data C emission failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" emit c.header "${model_prepared}"
          --arg "\"model\"" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${blob_header}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "external-data C header emission failed (${result}):\n${error}")
endif()
file(READ "${blob_source}" emitted_blob_source)
file(READ "${blob_header}" emitted_blob_header)
if(NOT emitted_blob_source MATCHES
   "const int8_t\\* [^\n]+ = \\(const int8_t\\*\\)\\(const void\\*\\)\\(joggle_data_model \\+ 0\\);" OR
   NOT emitted_blob_source MATCHES
   "const float\\* [^\n]+ = \\(const float\\*\\)\\(const void\\*\\)\\(joggle_data_model \\+ 4\\);" OR
   emitted_blob_source MATCHES "static const unsigned char joggle_data_" OR
   emitted_blob_source MATCHES "extern const unsigned char joggle_data_" OR
   NOT emitted_blob_header MATCHES
       "kernel_weights\\(const unsigned char\\* joggle_data_model, float\\* joggle_result\\);")
  message(FATAL_ERROR
          "external-data C interface did not expose the raw blob parameter:\n"
          "${emitted_blob_header}\n${emitted_blob_source}")
endif()
file(READ "${header}" emitted_header)
if(NOT emitted_header MATCHES
   "int64_t kernel_abi_probe\\(int64_t i, int64_t n, int32_t x\\);")
  message(FATAL_ERROR
          "C header did not apply its scalar ABI structurally:\n${emitted_header}")
endif()
if(NOT emitted_header MATCHES "float affine_kernel\\(float x\\);" OR
   emitted_header MATCHES "kernel_affine")
  message(FATAL_ERROR
          "C header did not honor the function-owned ABI name:\n"
          "${emitted_header}")
endif()
if(NOT emitted_header MATCHES
   "void kernel_split\\(int64_t x, int64_t\\* joggle_result_0, int64_t\\* joggle_result_1\\);" OR
   NOT emitted_header MATCHES
   "void kernel_duplicate\\(const float\\* x, float\\* joggle_result_0, float\\* joggle_result_1\\);")
  message(FATAL_ERROR
          "C header did not derive its multi-result ABI structurally:\n"
          "${emitted_header}")
endif()
if(emitted_header MATCHES "kernel_noop")
  message(FATAL_ERROR
          "C header exposed a local zero-result helper:\n${emitted_header}")
endif()
file(READ "${source}" emitted_source)
string(REGEX MATCH "int64_t kernel_steps\\(void\\) \\{[^}]*\\}"
       steps_source "${emitted_source}")
string(REGEX MATCH "int64_t kernel_select\\(void\\) \\{[^}]*\\}"
       select_source "${emitted_source}")
string(REGEX MATCH "int64_t kernel_captured_steps\\(void\\) \\{[^}]*\\}"
       captured_source "${emitted_source}")
if(steps_source STREQUAL "" OR select_source STREQUAL "" OR
   captured_source STREQUAL "" OR
   steps_source MATCHES "for \\(.*\\)" OR
   select_source MATCHES "for \\(.*\\)" OR
   captured_source MATCHES "for \\(.*\\)")
  message(FATAL_ERROR
          "C preparation did not fold pure static control:\n"
          "${steps_source}\n${select_source}\n${captured_source}")
endif()
if(NOT emitted_source MATCHES "#include <math.h>" OR
   emitted_source MATCHES "math_round_even")
  message(FATAL_ERROR
          "C source did not use module-declared math bindings:\n"
          "${emitted_source}")
endif()
if(NOT emitted_source MATCHES "for \\(int64_t joggle_i = 0;")
  message(FATAL_ERROR
          "C source did not use the configured index ABI for fixed storage:\n"
          "${emitted_source}")
endif()
string(FIND "${emitted_source}"
       "bool kernel_logical(int64_t a, int64_t b) {\n  if"
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
  COMMAND "${CC}" -std=c99 -Wall -Wextra -Wstrict-prototypes -Werror
          -I "${ROOT}" "${blob_source}" "${BLOB_HARNESS}" -lm
          -o "${blob_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "external-data C did not compile (${result}):\n${output}${error}\n"
          "${emitted_blob_source}")
endif()
execute_process(
  COMMAND "${blob_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "external-data C returned the wrong result (${result}):\n"
          "${output}${error}")
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
