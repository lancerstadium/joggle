if(NOT DEFINED TOOL OR NOT DEFINED CC OR NOT DEFINED MODEL OR
   NOT DEFINED HARNESS OR NOT DEFINED MODULES OR NOT DEFINED ROOT)
  message(FATAL_ERROR
          "sat C test requires TOOL, CC, MODEL, HARNESS, MODULES, ROOT")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")
set(prepared "${ROOT}/prepared.jog")
set(prepared_again "${ROOT}/prepared-again.jog")
set(source "${ROOT}/model.c")
set(header "${ROOT}/model.h")
set(program "${ROOT}/model")

execute_process(
  COMMAND "${TOOL}" run sat.c.prepare "${MODEL}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${prepared}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "sat C preparation failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run sat.c.prepare "${prepared}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${prepared_again}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "repeated sat C preparation failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files
          "${prepared}" "${prepared_again}"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "sat C preparation is not idempotent")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.source "${prepared}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "sat C emission failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" emit c.header "${prepared}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${header}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "sat C header emission failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CC}" -std=c99 -Wall -Wextra -Werror -Wstrict-prototypes
          -include "${header}"
          "${source}" "${HARNESS}" -o "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  file(READ "${source}" emitted)
  message(FATAL_ERROR
          "generated sat C did not compile (${result}):\n"
          "${output}${error}\n${emitted}")
endif()
execute_process(
  COMMAND "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "generated sat C returned the wrong result (${result}):\n"
          "${output}${error}")
endif()

file(REMOVE_RECURSE "${ROOT}")
