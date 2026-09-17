include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

joggle_require("compact extension requires TOOL, CC, MODEL, HARNESS, module roots, and ROOT" VARS TOOL CC MODEL HARNESS MODULES EXTENSIONS ROOT)

joggle_workspace("${ROOT}")
set(selected "${ROOT}/selected.jog")
set(staged "${ROOT}/staged.jog")
set(prepared "${ROOT}/prepared.jog")
set(source "${ROOT}/model.c")
set(program "${ROOT}/model")

joggle_run("compact selection failed"
  COMMAND "${TOOL}" run compact.apply "${MODEL}"
          --arg "{max_extra_elems: 0}"
          -M "${EXTENSIONS}" -M "${MODULES}"
  OUTPUT_FILE "${selected}"
  ERROR_VARIABLE error)
file(READ "${selected}" text)
string(REGEX MATCHALL
       "opt.instance: \\{\"fn\": \"compact.nn.conv2d\""
       instances "${text}")
list(LENGTH instances instance_count)
if(NOT instance_count EQUAL 1)
  message(FATAL_ERROR
          "compact selection expected one biased-convolution instance:\n${text}")
endif()
if(text MATCHES "spatial.nn.conv2d" OR
   NOT text MATCHES "return nn.conv2d\\(x, weight")
  message(FATAL_ERROR
          "configured selection did not leave the unmatched call canonical:\n${text}")
endif()

joggle_run("staged policy selection failed"
  COMMAND "${TOOL}" run compact.apply "${MODEL}"
          --arg "{max_extra_elems: 100}"
          -M "${EXTENSIONS}" -M "${MODULES}"
  OUTPUT_FILE "${staged}"
  ERROR_VARIABLE error)
file(READ "${staged}" staged_text)
if(staged_text MATCHES "\"fn\": \"compact.nn.conv2d\"" OR
   staged_text MATCHES "spatial.nn.conv2d" OR
   NOT staged_text MATCHES "return nn.conv2d\\(x, weight")
  message(FATAL_ERROR
          "large budget did not leave calls canonical:\n${staged_text}")
endif()

joggle_run("compact preparation failed"
  COMMAND "${TOOL}" run c.prepare mem.plan "${selected}"
          -M "${EXTENSIONS}" -M "${MODULES}"
  OUTPUT_FILE "${prepared}"
  ERROR_VARIABLE error)
execute_process(
  COMMAND "${TOOL}" query mem.buffers "${prepared}"
          -M "${EXTENSIONS}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE buffers
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0 OR NOT buffers STREQUAL "0\n")
  message(FATAL_ERROR
          "compact body materialized an intermediate (${result}):\n"
          "${buffers}${error}")
endif()

joggle_run("compact C emission failed"
  COMMAND "${TOOL}" emit c.source "${prepared}"
          -M "${EXTENSIONS}" -M "${MODULES}"
  OUTPUT_FILE "${source}"
  ERROR_VARIABLE error)
file(READ "${source}" text)
if(text MATCHES "model_(bias|activate)" OR
   text MATCHES "(^|[^A-Za-z0-9_])(joggle_|jog_|v_[A-Za-z0-9])")
  message(FATAL_ERROR
          "compact C retained a staged helper or opaque prefix:\n${text}")
endif()

joggle_run("compact C did not compile"
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${source}" "${HARNESS}" -lm -o "${program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
joggle_run("compact C returned the wrong result"
  COMMAND "${program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
