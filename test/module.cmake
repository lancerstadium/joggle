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
    "base\nc\nir\nmath\nmem\nnn\nonnx\nonnx.nn\nopt\nquant\nsat\nstat\ntensor\ntflite\ntflite.nn\n")
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

set(upgrade_source "${TEST_ROOT}.upgrade")
set(incompatible_source "${TEST_ROOT}.incompatible")
set(invalid_source "${TEST_ROOT}.invalid-upgrade")
set(dependent_source "${TEST_ROOT}.dependent")
file(REMOVE_RECURSE "${upgrade_source}" "${incompatible_source}"
                    "${invalid_source}" "${dependent_source}")
file(MAKE_DIRECTORY "${upgrade_source}" "${incompatible_source}"
                    "${invalid_source}" "${dependent_source}")
file(COPY "${BUILD_ROOT}/sample/" DESTINATION "${upgrade_source}")
file(READ "${upgrade_source}/module.jog" upgrade_module)
string(REPLACE "fn keep<T: Ty>(x: T) -> T;"
               "fn keep<U: Ty>(x: U) -> U;"
               upgrade_module "${upgrade_module}")
file(WRITE "${upgrade_source}/module.jog"
     "${upgrade_module}\nfn added(x: i32) -> i32;\n")
invoke(ok "${TOOL}" module upgrade "${upgrade_source}" "${TEST_ROOT}"
       -M "${BUILD_ROOT}")
invoke(ok "${TOOL}" module check sample -M "${TEST_ROOT}")
file(READ "${TEST_ROOT}/sample/module.jog" upgraded_source)
if(NOT upgraded_source MATCHES "fn added\\(x: i32\\) -> i32;")
  message(FATAL_ERROR "compatible upgrade was not committed")
endif()

file(COPY "${upgrade_source}/" DESTINATION "${incompatible_source}")
file(READ "${incompatible_source}/module.jog" incompatible_module)
string(REPLACE "fn ping(x: i32) -> i32;"
               "fn ping(x: i64) -> i64;"
               incompatible_module "${incompatible_module}")
file(WRITE "${incompatible_source}/module.jog" "${incompatible_module}")
invoke(fail "${TOOL}" module upgrade "${incompatible_source}" "${TEST_ROOT}"
       -M "${BUILD_ROOT}")
file(READ "${TEST_ROOT}/sample/module.jog" retained_source)
if(NOT retained_source STREQUAL upgraded_source)
  message(FATAL_ERROR "failed upgrade changed the installed module")
endif()

file(COPY "${upgrade_source}/" DESTINATION "${invalid_source}")
file(READ "${invalid_source}/module.jog" invalid_module)
string(REPLACE "module sample\n" "module sample\nuse absent\n"
               invalid_module "${invalid_module}")
file(WRITE "${invalid_source}/module.jog" "${invalid_module}")
invoke(fail "${TOOL}" module upgrade "${invalid_source}" "${TEST_ROOT}"
       -M "${BUILD_ROOT}")
file(READ "${TEST_ROOT}/sample/module.jog" retained_source)
if(NOT retained_source STREQUAL upgraded_source)
  message(FATAL_ERROR "invalid staged upgrade changed the installed module")
endif()

invoke(fail "${TOOL}" module install "${BUILD_ROOT}/bad" "${TEST_ROOT}"
       -M "${BUILD_ROOT}")
if(EXISTS "${TEST_ROOT}/bad")
  message(FATAL_ERROR "invalid module was committed")
endif()

file(WRITE "${dependent_source}/module.jog"
     "module dependent\nuse sample\nfn call(x: i32) -> i32 { return sample.ping(x) }\n")
invoke(ok "${TOOL}" module install "${dependent_source}" "${TEST_ROOT}"
       -M "${TEST_ROOT}")
invoke(fail "${TOOL}" module uninstall sample "${TEST_ROOT}")
if(NOT EXISTS "${TEST_ROOT}/sample/module.jog" OR
   NOT EXISTS "${TEST_ROOT}/dependent/module.jog")
  message(FATAL_ERROR "blocked uninstall changed the installed modules")
endif()
invoke(ok "${TOOL}" module uninstall dependent "${TEST_ROOT}")
invoke(ok "${TOOL}" module uninstall sample "${TEST_ROOT}")
if(EXISTS "${TEST_ROOT}/sample")
  message(FATAL_ERROR "uninstalled module remains")
endif()
invoke(fail "${TOOL}" module uninstall sample "${TEST_ROOT}")

file(REMOVE_RECURSE "${upgrade_source}" "${incompatible_source}"
                    "${invalid_source}" "${dependent_source}")

file(GLOB residue "${TEST_ROOT}/*" "${TEST_ROOT}/.*")
foreach(path IN LISTS residue)
  get_filename_component(name "${path}" NAME)
  if(NOT name STREQUAL "." AND NOT name STREQUAL "..")
    message(FATAL_ERROR "module transaction left residue: ${path}")
  endif()
endforeach()
file(REMOVE_RECURSE "${TEST_ROOT}")
