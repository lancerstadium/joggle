include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

joggle_require("quantized C test requires TOOL, CC, MODEL, HARNESS, MODULES, ROOT" VARS TOOL CC MODEL HARNESS MODULES ROOT)

joggle_workspace("${ROOT}")
set(prepared "${ROOT}/model.jog")
set(source "${ROOT}/model.c")
set(header "${ROOT}/model.h")
set(program "${ROOT}/model")

joggle_run("quantized C preparation failed"
  COMMAND "${TOOL}" run c.prepare "${MODEL}" -M "${MODULES}"
  OUTPUT_FILE "${prepared}"
  ERROR_VARIABLE error)

joggle_run("quantized C emission failed"
  COMMAND "${TOOL}" emit c.source "${prepared}" -M "${MODULES}"
  OUTPUT_FILE "${source}"
  ERROR_VARIABLE error)

joggle_run("quantized C header failed"
  COMMAND "${TOOL}" emit c.header "${prepared}" -M "${MODULES}"
  OUTPUT_FILE "${header}"
  ERROR_VARIABLE error)

joggle_run("quantized C did not compile"
  COMMAND "${CC}" -std=c99 -Wall -Wextra -Wstrict-prototypes -Werror
          -include "${header}" "${source}" "${HARNESS}" -lm -o "${program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

joggle_run("quantized C returned the wrong result"
  COMMAND "${program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
