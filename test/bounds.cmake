include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

joggle_require("bounds test requires TOOL, models, MODULES, and ROOT" VARS TOOL MODEL FOLD_MODEL CUSTOM_MODEL MODULES ROOT)

joggle_workspace("${ROOT}")
set(bounded "${ROOT}/bounded.jog")
set(stable "${ROOT}/stable.jog")
set(folded "${ROOT}/folded.jog")
set(custom "${ROOT}/custom.jog")

joggle_run("bounds query failed"
  COMMAND "${TOOL}" query bounds.report "${MODEL}" -M "${MODULES}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT output MATCHES
   "\"[0-9]+\": \\{\"fn\": \"kernel\", \"hi\": 5, \"lo\": 2, \"name\": \"i\", \"type\": \"index\"\\}" OR
   NOT output MATCHES
   "\"[0-9]+\": \\{\"fn\": \"kernel\", \"hi\": 19, \"lo\": 7, \"name\": \"j\", \"type\": \"index\"\\}" OR
   NOT output MATCHES
   "\"[0-9]+\": \\{\"fn\": \"choose\", \"hi\": 8, \"lo\": -2, \"name\": \"selected\", \"type\": \"int\"\\}" OR
   output MATCHES "\"name\": \"wrapped\"" OR
   output MATCHES "\"name\": \"product\"")
  message(FATAL_ERROR "unexpected integer bounds report:\n${output}")
endif()

joggle_run("bounds folding failed"
  COMMAND "${TOOL}" run bounds.fold "${FOLD_MODEL}" -M "${MODULES}"
  OUTPUT_FILE "${bounded}"
  ERROR_VARIABLE error)
file(READ "${bounded}" text)
if(text MATCHES "if i >= 2" OR NOT text MATCHES "if j < 10" OR
   text MATCHES "tensor.dim" OR NOT text MATCHES "return 3")
  message(FATAL_ERROR
          "bounds folding changed the wrong conditions:\n${text}")
endif()
joggle_run("repeated bounds folding failed"
  COMMAND "${TOOL}" run bounds.fold "${bounded}" -M "${MODULES}"
  OUTPUT_FILE "${stable}"
  ERROR_VARIABLE error)
joggle_run("bounds folding is not idempotent"
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${bounded}" "${stable}")
joggle_run("custom bounds folding failed"
  COMMAND "${TOOL}" run bounds.fold "${CUSTOM_MODEL}" -M "${MODULES}"
  OUTPUT_FILE "${custom}"
  ERROR_VARIABLE error)
file(READ "${custom}" text)
if(NOT text MATCHES "return i32\\(1\\) < i32\\(2\\)")
  message(FATAL_ERROR
          "bounds folding assumed semantics for a user overload:\n${text}")
endif()
joggle_run("bounds cleanup failed"
  COMMAND "${TOOL}" run opt.fold opt.basic "${bounded}" -M "${MODULES}"
  OUTPUT_FILE "${folded}"
  ERROR_VARIABLE error)
file(READ "${folded}" text)
if(NOT text MATCHES "var total = 14" OR
   NOT text MATCHES "var rejected = 14" OR
   text MATCHES "if i >= 2|if i == 9")
  message(FATAL_ERROR
          "bounds facts did not compose with ordinary folding:\n${text}")
endif()
