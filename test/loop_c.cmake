# Exercise ONNX conversion, Jog serialization, C lowering, and execution.
set(NAME "Loop")
set(CONVERT ON)
set(WANT_HEADER ON)
include("${CMAKE_CURRENT_LIST_DIR}/c_pipeline.cmake")

foreach(shape_case a b c d e f g)
  joggle_run("invalid reshape ${shape_case} was accepted"
    COMMAND "${program}" "${shape_case}" EXPECT_FAIL ERROR_VARIABLE failure)
  joggle_expect("invalid reshape did not reach its validation assertion"
    TEXT "${failure}" MATCHES "[Aa]ssertion")
endforeach()

# Static placement covers independent buffers as well as planned reuse slots.
joggle_run("Loop static placement failed"
  COMMAND "${TOOL}" run c.place "${planned}" --arg "\"static\""
          -M "${MODULES}" OUTPUT_FILE "${ROOT}/static.jog")
joggle_run("Loop static C emission failed"
  COMMAND "${TOOL}" emit c.source "${ROOT}/static.jog" -M "${MODULES}"
  OUTPUT_FILE "${ROOT}/static.c")
file(READ "${ROOT}/static.c" static_source)
if(static_source MATCHES "\n[ ]+(float|int32_t|int64_t|bool) [A-Za-z0-9_]+\\[[0-9]+\\];")
  message(FATAL_ERROR "static placement retained an automatic tensor buffer")
endif()
joggle_run("Loop static C did not compile"
  COMMAND "${CC}" -std=c11 -O2 -Wall -Wextra -Werror -pedantic-errors
          "${ROOT}/static.c" "${HARNESS}" -o "${ROOT}/static-model")
joggle_run("Loop static C returned the wrong result"
  COMMAND "${ROOT}/static-model")
