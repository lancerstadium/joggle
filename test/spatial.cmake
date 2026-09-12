if(NOT DEFINED TOOL OR NOT DEFINED CC OR NOT DEFINED MODEL OR
   NOT DEFINED HARNESS OR NOT DEFINED MODULES OR NOT DEFINED EXAMPLES OR
   NOT DEFINED ROOT)
  message(FATAL_ERROR
          "spatial example requires TOOL, CC, MODEL, HARNESS, module roots, ROOT")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")
set(selected "${ROOT}/selected.jog")
set(prepared "${ROOT}/prepared.jog")
set(source "${ROOT}/model.c")
set(program "${ROOT}/model")

execute_process(
  COMMAND "${TOOL}" run spatial.apply "${MODEL}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${selected}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "spatial selection failed (${result}):\n${error}")
endif()
file(READ "${selected}" text)
if(NOT text MATCHES
   "for n in [^\n]+, m in [^\n]+, q in [^\n]+, r in [^\n]+, s in [^\n]+, oh in [^\n]+, ow in")
  message(FATAL_ERROR "spatial selection omitted its loop order:\n${text}")
endif()

execute_process(
  COMMAND "${TOOL}" run c.prepare mem.plan "${selected}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${prepared}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "spatial preparation failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" emit c.source "${prepared}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "spatial C emission failed (${result}):\n${error}")
endif()
file(READ "${source}" text)
if(NOT text MATCHES
   "void model_main\\(const float\\* x, const float\\* weight, float\\* out\\)")
  message(FATAL_ERROR
          "spatial C did not preserve the public function and value names:\n${text}")
endif()
if(text MATCHES "(^|[^A-Za-z0-9_])(jog_|v_[0-9])")
  message(FATAL_ERROR "spatial C introduced an opaque generated prefix:\n${text}")
endif()
execute_process(
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${source}" "${HARNESS}" -lm -o "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "spatial C did not compile (${result}):\n${output}${error}")
endif()
execute_process(
  COMMAND "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "spatial C returned the wrong result (${result}):\n${output}${error}")
endif()
