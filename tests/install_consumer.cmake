if(NOT DEFINED JOGGLE_BUILD_DIR OR NOT DEFINED JOGGLE_SOURCE_DIR)
  message(FATAL_ERROR "install consumer paths were not provided")
endif()

if(NOT DEFINED JOGGLE_INSTALL_CASE)
  set(JOGGLE_INSTALL_CASE "shared")
endif()

if(JOGGLE_CONFIGURE_STATIC)
  file(REMOVE_RECURSE "${JOGGLE_BUILD_DIR}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
            -S "${JOGGLE_SOURCE_DIR}"
            -B "${JOGGLE_BUILD_DIR}"
            -DBUILD_SHARED_LIBS=OFF
            -DJOGGLE_BUILD_TESTS=OFF
    RESULT_VARIABLE static_configure_result
  )
  if(NOT static_configure_result EQUAL 0)
    message(FATAL_ERROR "static Joggle configuration failed")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${JOGGLE_BUILD_DIR}"
    RESULT_VARIABLE static_build_result
  )
  if(NOT static_build_result EQUAL 0)
    message(FATAL_ERROR "static Joggle build failed")
  endif()
endif()

set(install_dir "${JOGGLE_BUILD_DIR}/test-${JOGGLE_INSTALL_CASE}-install")
set(consumer_build "${JOGGLE_BUILD_DIR}/test-${JOGGLE_INSTALL_CASE}-consumer")
set(module_root "${JOGGLE_BUILD_DIR}/test-${JOGGLE_INSTALL_CASE}-modules")
file(REMOVE_RECURSE "${install_dir}" "${consumer_build}" "${module_root}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${JOGGLE_BUILD_DIR}"
          --prefix "${install_dir}"
  RESULT_VARIABLE install_result
)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "Joggle installation failed")
endif()

file(GLOB source_public_headers
  RELATIVE "${JOGGLE_SOURCE_DIR}/include/joggle"
  "${JOGGLE_SOURCE_DIR}/include/joggle/*.h")
file(GLOB installed_public_headers
  RELATIVE "${install_dir}/include/joggle"
  "${install_dir}/include/joggle/*.h")
list(SORT source_public_headers)
list(SORT installed_public_headers)
if(NOT source_public_headers STREQUAL installed_public_headers)
  message(FATAL_ERROR
    "installed public header set differs from the source header set")
endif()
foreach(header IN LISTS source_public_headers)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files
            "${JOGGLE_SOURCE_DIR}/include/joggle/${header}"
            "${install_dir}/include/joggle/${header}"
    RESULT_VARIABLE header_compare_result
  )
  if(NOT header_compare_result EQUAL 0)
    message(FATAL_ERROR
      "installed public header is stale or modified: ${header}")
  endif()
endforeach()

find_program(installed_cli NAMES joggle joggle.exe
  PATHS "${install_dir}/bin" NO_DEFAULT_PATH)
if(NOT installed_cli)
  message(FATAL_ERROR "installed Joggle CLI was not found")
endif()

set(installed_tensor
  "${install_dir}/share/joggle/modules/tensor/module.joggle")
if(NOT EXISTS "${installed_tensor}")
  message(FATAL_ERROR "installed tensor Module was not found")
endif()
execute_process(
  COMMAND "${installed_cli}" check "${installed_tensor}"
  RESULT_VARIABLE tensor_cli_result
)
if(NOT tensor_cli_result EQUAL 0)
  message(FATAL_ERROR "installed tensor Module failed validation")
endif()

execute_process(
  COMMAND "${installed_cli}" check
          "${JOGGLE_SOURCE_DIR}/tests/consumer/module.joggle"
  RESULT_VARIABLE cli_result
)
if(NOT cli_result EQUAL 0)
  message(FATAL_ERROR "installed Joggle CLI failed")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
          -S "${JOGGLE_SOURCE_DIR}/tests/consumer"
          -B "${consumer_build}"
          -DCMAKE_PREFIX_PATH=${install_dir}
  RESULT_VARIABLE configure_result
)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "installed Joggle consumer configuration failed")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}"
  RESULT_VARIABLE build_result
)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "installed Joggle consumer build failed")
endif()

set(rebuild_source
  "${JOGGLE_BUILD_DIR}/test-${JOGGLE_INSTALL_CASE}-rebuild-source")
set(rebuild_build
  "${JOGGLE_BUILD_DIR}/test-${JOGGLE_INSTALL_CASE}-rebuild-build")
file(REMOVE_RECURSE "${rebuild_source}" "${rebuild_build}")
file(MAKE_DIRECTORY "${rebuild_source}")
file(COPY_FILE "${JOGGLE_SOURCE_DIR}/tests/consumer/module.joggle"
  "${rebuild_source}/module.joggle")
file(COPY_FILE "${JOGGLE_SOURCE_DIR}/tests/native_rebuild_consumer/CMakeLists.txt"
  "${rebuild_source}/CMakeLists.txt")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
          -S "${rebuild_source}"
          -B "${rebuild_build}"
          -DCMAKE_PREFIX_PATH=${install_dir}
          -DJOGGLE_NATIVE_SOURCE=${JOGGLE_SOURCE_DIR}/tests/consumer/native.cpp
  RESULT_VARIABLE rebuild_configure_result
)
if(NOT rebuild_configure_result EQUAL 0)
  message(FATAL_ERROR "native rebuild test configuration failed")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${rebuild_build}"
  RESULT_VARIABLE first_rebuild_result
)
if(NOT first_rebuild_result EQUAL 0)
  message(FATAL_ERROR "initial native rebuild test build failed")
endif()
set(identity_source "${rebuild_build}/rebuild_native_module_identity.cpp")
file(READ "${identity_source}" first_identity)
file(READ "${rebuild_source}/module.joggle" changed_module)
set(original_module "${changed_module}")
string(REPLACE "  fn keep" "  type extra();\n\n  fn keep"
  changed_module "${changed_module}")
if(changed_module STREQUAL original_module)
  message(FATAL_ERROR "native rebuild fixture did not change its Module")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
file(WRITE "${rebuild_source}/module.joggle" "${changed_module}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${rebuild_build}"
  RESULT_VARIABLE second_rebuild_result
)
if(NOT second_rebuild_result EQUAL 0)
  message(FATAL_ERROR "native did not rebuild after its Module changed")
endif()
file(READ "${identity_source}" second_identity)
if(first_identity STREQUAL second_identity)
  message(FATAL_ERROR
    "native identity did not change after canonical Module content changed")
endif()
file(READ "${rebuild_build}/native-path.txt" rebuilt_native)
string(STRIP "${rebuilt_native}" rebuilt_native)
execute_process(
  COMMAND "${installed_cli}" check
          "${JOGGLE_SOURCE_DIR}/tests/consumer/module.joggle"
          --native "${rebuilt_native}"
  RESULT_VARIABLE stale_native_result
  ERROR_VARIABLE stale_native_error
)
string(FIND "${stale_native_error}" "targets 'external@1.0.0#"
  stale_native_position)
if(stale_native_result EQUAL 0 OR stale_native_position EQUAL -1)
  message(FATAL_ERROR
    "native rebuilt for changed Module was accepted by the old Module:\n"
    "${stale_native_error}")
endif()

file(READ "${consumer_build}/native-path.txt" native_path)
string(STRIP "${native_path}" native_path)
file(READ "${consumer_build}/consumer-path.txt" consumer_path)
string(STRIP "${consumer_path}" consumer_path)
execute_process(
  COMMAND "${installed_cli}" install
          "${JOGGLE_SOURCE_DIR}/tests/consumer/module.joggle"
          --native "${native_path}" --root "${module_root}"
  RESULT_VARIABLE install_result
  OUTPUT_VARIABLE installed_module
  ERROR_VARIABLE install_error
)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "external Module installation failed:\n${install_error}")
endif()
string(STRIP "${installed_module}" installed_module)

execute_process(
  COMMAND "${consumer_path}" "${installed_module}" "${module_root}"
  RESULT_VARIABLE run_result
)
if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "installed Joggle consumer execution failed")
endif()
