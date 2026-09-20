# Shared pipeline for the "<operation> C execution" tests.
#
# Eight tests ran the same steps: prepare the model with c.prepare, plan storage
# with mem.plan, require an empty C frontier, emit C, compile it strictly, run
# it, and require a zero exit status. They differed only in the model, the
# harness, and one to three assertions peculiar to the operation, so the
# pipeline lives here once and each test adds only its own assertions.
#
# An including script sets:
#   NAME          label used in diagnostics (default "C")
#   CONVERT       reach c.prepare through the ONNX frontend first
#   WANT_HEADER   also emit c.header, for an assertion on the declared ABI
#
# and may define the macro c_pipeline_checks(), which runs once the artifacts
# exist, with these variables in scope:
#   ${planned}  ${source}  ${header}  ${api}  ${frontier}
#
# The hook is a macro rather than a list of patterns on purpose. joggle_expect
# takes one pattern per call because a CMake list cannot carry a regex holding a
# bracket expression, and the patterns these tests need are full of them.

include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

if(NOT DEFINED NAME)
  set(NAME "C")
endif()

joggle_require("${NAME} C test requires TOOL, CC, MODEL, HARNESS, MODULES, ROOT"
               VARS TOOL CC MODEL HARNESS MODULES ROOT)

joggle_workspace("${ROOT}")
set(prepared "${ROOT}/prepared.jog")
set(planned "${ROOT}/planned.jog")
set(source "${ROOT}/model.c")
set(header "${ROOT}/model.h")
set(program "${ROOT}/model")

# One operation reaches c.prepare through the ONNX frontend, so the conversion
# stage exists only for the tests that ask for it.
if(CONVERT)
  set(converted "${ROOT}/converted.jog")
  joggle_run("${NAME} ONNX conversion failed"
    COMMAND "${TOOL}" run onnx.nn.infer onnx.nn.convert "${MODEL}"
            -M "${MODULES}"
    OUTPUT_FILE "${converted}")
  set(c_pipeline_input "${converted}")
else()
  set(c_pipeline_input "${MODEL}")
endif()

joggle_run("${NAME} C preparation failed"
  COMMAND "${TOOL}" run c.prepare "${c_pipeline_input}" -M "${MODULES}"
  OUTPUT_FILE "${prepared}")

joggle_run("${NAME} C planning failed"
  COMMAND "${TOOL}" run mem.plan "${prepared}" -M "${MODULES}"
  OUTPUT_FILE "${planned}")

joggle_run("${NAME} C frontier query failed"
  COMMAND "${TOOL}" query c.frontier "${planned}" -M "${MODULES}"
  OUTPUT_VARIABLE frontier)
if(NOT frontier STREQUAL "[]\n")
  message(FATAL_ERROR
          "prepared ${NAME} C retains a capability frontier:\n${frontier}")
endif()

joggle_run("${NAME} C API query failed"
  COMMAND "${TOOL}" query c.api "${planned}" -M "${MODULES}"
  OUTPUT_VARIABLE api)

joggle_run("${NAME} C source emission failed"
  COMMAND "${TOOL}" emit c.source "${planned}" -M "${MODULES}"
  OUTPUT_FILE "${source}")

if(WANT_HEADER)
  joggle_run("${NAME} C header emission failed"
    COMMAND "${TOOL}" emit c.header "${planned}" -M "${MODULES}"
    OUTPUT_FILE "${header}")
endif()

if(COMMAND c_pipeline_checks)
  c_pipeline_checks()
endif()

joggle_run("${NAME} C did not compile"
  COMMAND "${CC}" -std=c11 -O2 -Wall -Wextra -Werror -pedantic-errors
          "${source}" "${HARNESS}" -o "${program}" ${LINK_LIBRARIES})

joggle_run("${NAME} C returned the wrong result"
  COMMAND "${program}")
