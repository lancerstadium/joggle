if(NOT DEFINED TOOL OR NOT DEFINED CC OR NOT DEFINED MODEL OR
   NOT DEFINED HARNESS OR NOT DEFINED MODULES OR NOT DEFINED ROOT)
  message(FATAL_ERROR
          "memory planning test requires TOOL, CC, MODEL, HARNESS, MODULES, ROOT")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")

set(prepared "${ROOT}/prepared.jog")
set(planned "${ROOT}/planned.jog")
set(planned_again "${ROOT}/planned-again.jog")
set(source "${ROOT}/model.c")
set(program "${ROOT}/model")

execute_process(
  COMMAND "${TOOL}" run c.prepare "${MODEL}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${prepared}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "C preparation failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" run mem.plan "${prepared}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${planned}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "memory planning failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" query mem.buffers "${planned}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE buffers
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0 OR NOT buffers STREQUAL "2\n")
  message(FATAL_ERROR
          "memory planning used the wrong buffer count (${result}):\n"
          "${buffers}${error}")
endif()

execute_process(
  COMMAND "${TOOL}" query stat.summary "${planned}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE summary
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0 OR
   NOT summary MATCHES "\"mem_slots\": 2" OR
   NOT summary MATCHES "\"mem_elems\": 8")
  message(FATAL_ERROR
          "planned structural summary is invalid (${result}):\n"
          "${summary}${error}")
endif()

execute_process(
  COMMAND "${TOOL}" run mem.plan "${planned}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${planned_again}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "repeated memory planning failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${planned}" "${planned_again}"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "memory planning is not idempotent")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.source "${planned}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "planned C emission failed (${result}):\n${error}")
endif()

file(READ "${source}" emitted)
string(REGEX MATCHALL "float jog_mem_f32_[0-9]+" buffers "${emitted}")
list(LENGTH buffers buffer_count)
if(NOT buffer_count EQUAL 2)
  message(FATAL_ERROR
          "planned C did not declare exactly two buffers:\n${emitted}")
endif()

execute_process(
  COMMAND "${CC}" -std=c99 -Wall -Wextra -Werror
          "${source}" "${HARNESS}" -o "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "planned C did not compile (${result}):\n${output}${error}\n${emitted}")
endif()
execute_process(
  COMMAND "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "planned C returned the wrong result (${result}):\n${output}${error}")
endif()

file(REMOVE_RECURSE "${ROOT}")
