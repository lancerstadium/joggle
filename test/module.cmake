if(NOT DEFINED TOOL OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED BUILD_ROOT OR
   NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR
          "module test requires TOOL, SOURCE_ROOT, BUILD_ROOT, TEST_ROOT")
endif()

function(invoke expected)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  if(expected STREQUAL "ok" AND NOT result EQUAL 0)
    message(FATAL_ERROR
            "command failed (${result}): ${ARGN}\n${output}${error}")
  endif()
  if(expected STREQUAL "fail" AND result EQUAL 0)
    message(FATAL_ERROR "command unexpectedly succeeded: ${ARGN}\n${output}")
  endif()
  set(COMMAND_OUTPUT "${output}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")

invoke(ok "${TOOL}" module list -M "${SOURCE_ROOT}")
set(expected
    "base\nir\nmath\nnn\nonnx\nonnx.nn\nopt\nquant\nsat\ntensor\ntflite\ntflite.nn\n")
if(NOT COMMAND_OUTPUT STREQUAL expected)
  message(FATAL_ERROR "module list is not canonical:\n${COMMAND_OUTPUT}")
endif()

invoke(ok "${TOOL}" module check nn -M "${SOURCE_ROOT}")
invoke(ok "${TOOL}" module info tensor -M "${SOURCE_ROOT}")
if(NOT COMMAND_OUTPUT MATCHES
   "^module tensor\npath .+\nuse base\nsource module.jog\n$")
  message(FATAL_ERROR "unexpected module info:\n${COMMAND_OUTPUT}")
endif()

invoke(ok "${TOOL}" module install "${BUILD_ROOT}/sample" "${TEST_ROOT}"
       -M "${BUILD_ROOT}")
if(NOT EXISTS "${TEST_ROOT}/sample/module.jog")
  message(FATAL_ERROR "installed module is missing")
endif()
invoke(fail "${TOOL}" module install "${BUILD_ROOT}/sample" "${TEST_ROOT}"
       -M "${BUILD_ROOT}")
invoke(ok "${TOOL}" module check sample -M "${TEST_ROOT}")
invoke(ok "${TOOL}" module info sample -M "${TEST_ROOT}")
if(NOT COMMAND_OUTPUT MATCHES "native joggle_sample\\.(so|dylib|dll)\n")
  message(FATAL_ERROR "native library is not reported:\n${COMMAND_OUTPUT}")
endif()

invoke(fail "${TOOL}" module install "${BUILD_ROOT}/bad" "${TEST_ROOT}"
       -M "${BUILD_ROOT}")
if(EXISTS "${TEST_ROOT}/bad")
  message(FATAL_ERROR "invalid module was committed")
endif()

invoke(ok "${TOOL}" module uninstall sample "${TEST_ROOT}")
if(EXISTS "${TEST_ROOT}/sample")
  message(FATAL_ERROR "uninstalled module remains")
endif()
invoke(fail "${TOOL}" module uninstall sample "${TEST_ROOT}")

file(GLOB residue "${TEST_ROOT}/*" "${TEST_ROOT}/.*")
foreach(path IN LISTS residue)
  get_filename_component(name "${path}" NAME)
  if(NOT name STREQUAL "." AND NOT name STREQUAL "..")
    message(FATAL_ERROR "module transaction left residue: ${path}")
  endif()
endforeach()
file(REMOVE_RECURSE "${TEST_ROOT}")
