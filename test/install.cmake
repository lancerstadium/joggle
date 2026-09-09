if(NOT DEFINED BUILD_ROOT OR NOT DEFINED SOURCE_ROOT OR
   NOT DEFINED TEST_ROOT OR NOT DEFINED SUFFIX)
  message(FATAL_ERROR
          "install test requires BUILD_ROOT, SOURCE_ROOT, TEST_ROOT, SUFFIX")
endif()

function(invoke)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR
            "command failed (${result}): ${ARGN}\n${output}${error}")
  endif()
endfunction()

set(prefix "${TEST_ROOT}/prefix")
set(build "${TEST_ROOT}/build")
file(REMOVE_RECURSE "${TEST_ROOT}")

set(config_args)
set(build_args)
set(install_args)
if(DEFINED CONFIG AND NOT CONFIG STREQUAL "")
  list(APPEND config_args "-DCMAKE_BUILD_TYPE=${CONFIG}")
  list(APPEND build_args --config "${CONFIG}")
  list(APPEND install_args --config "${CONFIG}")
endif()

invoke("${CMAKE_COMMAND}" --install "${BUILD_ROOT}" --prefix "${prefix}"
       ${install_args})
invoke("${CMAKE_COMMAND}" -S "${SOURCE_ROOT}/test/consumer" -B "${build}"
       "-DCMAKE_PREFIX_PATH=${prefix}" ${config_args})
invoke("${CMAKE_COMMAND}" --build "${build}" --target consumer probe
       ${build_args})
set(tool "${prefix}/bin/joggle${SUFFIX}")
set(modules "${TEST_ROOT}/modules")
set(standard "${prefix}/share/joggle/modules")
invoke("${tool}" module install "${build}/package" "${modules}"
       -M "${standard}")
invoke("${tool}" module check probe -M "${modules}")
invoke("${build}/bin/consumer${SUFFIX}"
       "${modules}" "${standard}")
invoke("${tool}" module uninstall probe "${modules}")

file(REMOVE_RECURSE "${TEST_ROOT}")
