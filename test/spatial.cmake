if(NOT DEFINED TOOL OR NOT DEFINED CC OR NOT DEFINED MODEL OR
   NOT DEFINED HARNESS OR NOT DEFINED MODULES OR NOT DEFINED EXAMPLES OR
   NOT DEFINED ROOT)
  message(FATAL_ERROR
          "spatial example requires TOOL, CC, MODEL, HARNESS, module roots, ROOT")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")
set(canonical "${ROOT}/canonical.jog")
set(prepared "${ROOT}/prepared.jog")
set(source "${ROOT}/model.c")
set(program "${ROOT}/model")

execute_process(
  COMMAND "${TOOL}" run c.prepare mem.plan "${MODEL}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${canonical}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "canonical preparation failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run tile_pass.check_reorder "${canonical}"
          -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_QUIET
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "reorder contract failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run spatial.apply "${canonical}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${prepared}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "spatial scheduling failed (${result}):\n${error}")
endif()
file(READ "${prepared}" text)
if(text MATCHES "spatial.nn.conv2d" OR text MATCHES "use spatial")
  message(FATAL_ERROR
          "spatial scheduling retained an implementation override:\n${text}")
endif()
if(NOT text MATCHES
   "for n in [^\n]+, m in [^\n]+, q in [^\n]+, r in [^\n]+, s in [^\n]+, oh in [^\n]+, ow in")
  message(FATAL_ERROR "spatial pass omitted its loop order:\n${text}")
endif()
execute_process(
  COMMAND "${TOOL}" run tile_pass.check_spatial_axes "${prepared}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_QUIET
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "spatial axis dependence check failed (${result}):\n${error}")
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
   "void model_main\\(const float\\* x, const float\\* weight, float\\* (out|out_out)\\)")
  message(FATAL_ERROR
          "spatial C did not preserve the public function and value names:\n${text}")
endif()
if(text MATCHES "(^|[^A-Za-z0-9_])(joggle_|jog_|v_[A-Za-z0-9])")
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
