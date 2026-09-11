if(NOT DEFINED TOOL OR NOT DEFINED MODEL OR NOT DEFINED MODULES)
  message(FATAL_ERROR "bounds test requires TOOL, MODEL, and MODULES")
endif()

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
