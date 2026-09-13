if(NOT DEFINED TOOL OR NOT DEFINED MODEL OR NOT DEFINED FOLD_MODEL OR
   NOT DEFINED CUSTOM_MODEL OR NOT DEFINED MODULES OR NOT DEFINED ROOT)
  message(FATAL_ERROR
          "bounds test requires TOOL, models, MODULES, and ROOT")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")
set(bounded "${ROOT}/bounded.jog")
set(stable "${ROOT}/stable.jog")
set(folded "${ROOT}/folded.jog")
set(custom "${ROOT}/custom.jog")

execute_process(
  COMMAND "${TOOL}" query bounds.report "${MODEL}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "bounds query failed (${result}):\n${output}${error}")
endif()
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

execute_process(
  COMMAND "${TOOL}" run bounds.fold "${FOLD_MODEL}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${bounded}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "bounds folding failed (${result}):\n${error}")
endif()
file(READ "${bounded}" text)
if(text MATCHES "if i >= 2" OR NOT text MATCHES "if j < 10")
  message(FATAL_ERROR
          "bounds folding changed the wrong conditions:\n${text}")
endif()
execute_process(
  COMMAND "${TOOL}" run bounds.fold "${bounded}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${stable}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "repeated bounds folding failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${bounded}" "${stable}"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "bounds folding is not idempotent")
endif()
execute_process(
  COMMAND "${TOOL}" run bounds.fold "${CUSTOM_MODEL}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${custom}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "custom bounds folding failed (${result}):\n${error}")
endif()
file(READ "${custom}" text)
if(NOT text MATCHES "return i32\\(1\\) < i32\\(2\\)")
  message(FATAL_ERROR
          "bounds folding assumed semantics for a user overload:\n${text}")
endif()
execute_process(
  COMMAND "${TOOL}" run opt.fold opt.basic "${bounded}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${folded}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "bounds cleanup failed (${result}):\n${error}")
endif()
file(READ "${folded}" text)
if(NOT text MATCHES "var total = 14" OR
   NOT text MATCHES "var rejected = 14" OR
   text MATCHES "if i >= 2|if i == 9")
  message(FATAL_ERROR
          "bounds facts did not compose with ordinary folding:\n${text}")
endif()
