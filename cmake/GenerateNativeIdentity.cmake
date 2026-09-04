if(NOT DEFINED JOGGLE_CLI OR NOT DEFINED JOGGLE_MOD OR
   NOT DEFINED JOGGLE_OUTPUT)
  message(FATAL_ERROR "native identity generation is missing an argument")
endif()

execute_process(
  COMMAND "${JOGGLE_CLI}" fmt "${JOGGLE_MOD}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE canonical
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
    "cannot compute canonical identity for ${JOGGLE_MOD}:\n${error}")
endif()

string(REGEX MATCH
  "mod ([A-Za-z_][A-Za-z0-9_]*)@([0-9]+\\.[0-9]+\\.[0-9]+) \\{"
  header "${canonical}")
if(NOT header)
  message(FATAL_ERROR
    "canonical source has no Mod identity: ${JOGGLE_MOD}")
endif()
set(name "${CMAKE_MATCH_1}")
set(version "${CMAKE_MATCH_2}")
string(SHA256 digest "${canonical}")
set(identity "${name}@${version}#${digest}")

file(WRITE "${JOGGLE_OUTPUT}"
  "#include <joggle/detail/native.h>\n\n"
  "void joggle_mod(joggle::Compiler&, const joggle::Mod&, "
  "joggle::Diagnostics&);\n\n"
  "namespace joggle::detail {\n"
  "const char native_mod_identity[] = \"${identity}\";\n"
  "}\n\n"
  "#if defined(_WIN32)\n"
  "#define JOGGLE_NATIVE_EXPORT __declspec(dllexport)\n"
  "#else\n"
  "#define JOGGLE_NATIVE_EXPORT __attribute__((visibility(\"default\")))\n"
  "#endif\n\n"
  "extern \"C\" JOGGLE_NATIVE_EXPORT const joggle::detail::NativeLibrary*\n"
  "joggle_native_v1() {\n"
  "  static const joggle::detail::NativeLibrary library{joggle_mod};\n"
  "  return &library;\n"
  "}\n")
