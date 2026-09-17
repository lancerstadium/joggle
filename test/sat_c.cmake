include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

joggle_require("sat C test requires TOOL, CC, MODEL, HARNESS, MODULES, ROOT" VARS TOOL CC MODEL HARNESS MODULES ROOT)

joggle_workspace("${ROOT}")
set(prepared "${ROOT}/prepared.jog")
set(prepared_again "${ROOT}/prepared-again.jog")
set(source "${ROOT}/model.c")
set(header "${ROOT}/model.h")
set(program "${ROOT}/model")

joggle_run("sat C preparation failed"
  COMMAND "${TOOL}" run sat.c.prepare "${MODEL}" -M "${MODULES}"
  OUTPUT_FILE "${prepared}"
  ERROR_VARIABLE error)
joggle_run("repeated sat C preparation failed"
  COMMAND "${TOOL}" run sat.c.prepare "${prepared}" -M "${MODULES}"
  OUTPUT_FILE "${prepared_again}"
  ERROR_VARIABLE error)
joggle_run("sat C preparation is not idempotent"
  COMMAND "${CMAKE_COMMAND}" -E compare_files
          "${prepared}" "${prepared_again}")

joggle_run("sat C emission failed"
  COMMAND "${TOOL}" emit c.source "${prepared}" -M "${MODULES}"
  OUTPUT_FILE "${source}"
  ERROR_VARIABLE error)
joggle_run("sat C header emission failed"
  COMMAND "${TOOL}" emit c.header "${prepared}" -M "${MODULES}"
  OUTPUT_FILE "${header}"
  ERROR_VARIABLE error)
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
joggle_run("generated sat C returned the wrong result"
  COMMAND "${program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

file(REMOVE_RECURSE "${ROOT}")
