if(NOT DEFINED TOOL OR NOT DEFINED CC OR NOT DEFINED MODEL OR
   NOT DEFINED HARNESS OR NOT DEFINED MODULES OR NOT DEFINED ROOT)
  message(FATAL_ERROR
          "NMS C test requires TOOL, CC, MODEL, HARNESS, MODULES, ROOT")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")
set(prepared "${ROOT}/prepared.jog")
set(planned "${ROOT}/planned.jog")
set(source "${ROOT}/model.c")
set(header "${ROOT}/model.h")
set(program "${ROOT}/model")

execute_process(
  COMMAND "${TOOL}" run c.prepare "${MODEL}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_FILE "${prepared}" ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "NMS C preparation failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run mem.plan "${prepared}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_FILE "${planned}" ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "NMS C planning failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" query c.frontier "${planned}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_VARIABLE frontier ERROR_VARIABLE error
)
if(NOT result EQUAL 0 OR NOT frontier STREQUAL "[]\n")
  message(FATAL_ERROR
          "prepared NMS retains a capability frontier (${result}):\n"
          "${frontier}${error}")
endif()
execute_process(
  COMMAND "${TOOL}" emit c.source "${planned}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_FILE "${source}" ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "NMS C emission failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" emit c.header "${planned}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_FILE "${header}" ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "NMS C header failed (${result}):\n${error}")
endif()
file(READ "${header}" header_text)
if(NOT header_text MATCHES
   "int64_t\\* out_[0-9]+, int64_t\\* out_[0-9]+_dim_0")
  message(FATAL_ERROR "NMS C ABI omits its dynamic result extent:\n${header_text}")
endif()
execute_process(
  COMMAND "${CC}" -std=c11 -O2 -Wall -Wextra -Werror -pedantic-errors
          "${source}" "${HARNESS}" -o "${program}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "NMS C did not compile (${result}):\n${output}${error}")
endif()
execute_process(
  COMMAND "${program}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "NMS C returned the wrong selection (${result}):\n${output}${error}")
endif()
