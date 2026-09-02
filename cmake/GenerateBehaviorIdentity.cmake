if(NOT DEFINED JOGGLE_CLI OR NOT DEFINED JOGGLE_MODULE OR
   NOT DEFINED JOGGLE_OUTPUT)
  message(FATAL_ERROR "behavior identity generation is missing an argument")
endif()

execute_process(
  COMMAND "${JOGGLE_CLI}" fmt "${JOGGLE_MODULE}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE canonical
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
    "cannot compute canonical identity for ${JOGGLE_MODULE}:\n${error}")
endif()

string(REGEX MATCH
  "module ([A-Za-z_][A-Za-z0-9_]*)@([0-9]+\\.[0-9]+\\.[0-9]+) \\{"
  header "${canonical}")
if(NOT header)
  message(FATAL_ERROR
    "canonical source has no Module identity: ${JOGGLE_MODULE}")
endif()
set(name "${CMAKE_MATCH_1}")
set(version "${CMAKE_MATCH_2}")
string(SHA256 digest "${canonical}")
set(identity "${name}@${version}#${digest}")

file(WRITE "${JOGGLE_OUTPUT}"
  "#include <joggle/behavior.h>\n\n"
  "namespace joggle::detail {\n"
  "const char behavior_module_identity[] = \"${identity}\";\n"
  "}\n")
