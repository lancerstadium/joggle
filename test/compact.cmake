if(NOT DEFINED TOOL OR NOT DEFINED CC OR NOT DEFINED MODEL OR
   NOT DEFINED HARNESS OR NOT DEFINED MODULES OR NOT DEFINED EXAMPLES OR
   NOT DEFINED ROOT)
  message(FATAL_ERROR
          "compact example requires TOOL, CC, MODEL, HARNESS, module roots, and ROOT")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")
set(selected "${ROOT}/selected.jog")
set(staged "${ROOT}/staged.jog")
set(prepared "${ROOT}/prepared.jog")
set(source "${ROOT}/model.c")
set(program "${ROOT}/model")

execute_process(
  COMMAND "${TOOL}" run compact.apply "${MODEL}"
          --arg "{max_extra_elems: 0}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${selected}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "compact selection failed (${result}):\n${error}")
endif()
file(READ "${selected}" text)
string(REGEX MATCHALL
       "opt.instance: \\{\"fn\": \"compact.nn.conv2d\""
       instances "${text}")
list(LENGTH instances instance_count)
if(NOT instance_count EQUAL 1)
  message(FATAL_ERROR
          "compact selection expected one biased-convolution instance:\n${text}")
endif()
string(REGEX MATCHALL
       "opt.instance: \\{\"fn\": \"spatial.nn.conv2d\""
       spatial_instances "${text}")
list(LENGTH spatial_instances spatial_count)
if(NOT spatial_count EQUAL 1)
  message(FATAL_ERROR
          "configured selection did not retain the unbiased spatial body:\n${text}")
endif()

execute_process(
  COMMAND "${TOOL}" run compact.apply "${MODEL}"
          --arg "{max_extra_elems: 100}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${staged}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "staged policy selection failed (${result}):\n${error}")
endif()
file(READ "${staged}" staged_text)
string(REGEX MATCHALL
       "opt.instance: \\{\"fn\": \"spatial.nn.conv2d\""
       staged_instances "${staged_text}")
list(LENGTH staged_instances staged_count)
if(NOT staged_count EQUAL 2 OR
   staged_text MATCHES "\"fn\": \"compact.nn.conv2d\"")
  message(FATAL_ERROR
          "large budget did not select both staged bodies:\n${staged_text}")
endif()

execute_process(
  COMMAND "${TOOL}" run c.prepare mem.plan "${selected}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${prepared}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "compact preparation failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" query mem.buffers "${prepared}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE buffers
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0 OR NOT buffers STREQUAL "0\n")
  message(FATAL_ERROR
          "compact body materialized an intermediate (${result}):\n"
          "${buffers}${error}")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.source "${prepared}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "compact C emission failed (${result}):\n${error}")
endif()
file(READ "${source}" text)
if(text MATCHES "model_(bias|activate)" OR
   text MATCHES "(^|[^A-Za-z0-9_])(joggle_|jog_|v_[A-Za-z0-9])")
  message(FATAL_ERROR
          "compact C retained a staged helper or opaque prefix:\n${text}")
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
          "compact C did not compile (${result}):\n${output}${error}")
endif()
execute_process(
  COMMAND "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "compact C returned the wrong result (${result}):\n${output}${error}")
endif()
