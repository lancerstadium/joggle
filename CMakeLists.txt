cmake_minimum_required(VERSION 3.20)

project(Joggle VERSION 0.1.0 LANGUAGES CXX)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(JOGGLE_BUILD_TESTS "Build Joggle tests" ON)
option(JOGGLE_BUILD_ONNX "Build the optional ONNX inference importer" OFF)
option(JOGGLE_ONNX_RUNTIME_VALIDATION
  "Differentially validate ONNX import with Python ONNX Runtime" OFF)

if(JOGGLE_ONNX_RUNTIME_VALIDATION AND
   (NOT JOGGLE_BUILD_ONNX OR NOT JOGGLE_BUILD_TESTS))
  message(FATAL_ERROR
    "JOGGLE_ONNX_RUNTIME_VALIDATION requires JOGGLE_BUILD_ONNX and tests")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/language/prelude.joggle"
  JOGGLE_PRELUDE_SOURCE)
string(FIND "${JOGGLE_PRELUDE_SOURCE}" ")JOGGLE\""
  JOGGLE_PRELUDE_DELIMITER)
if(NOT JOGGLE_PRELUDE_DELIMITER EQUAL -1)
  message(FATAL_ERROR
    "language/prelude.joggle contains the reserved embed delimiter")
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(src/prelude_source.cpp.in
  "${CMAKE_CURRENT_BINARY_DIR}/generated/prelude_source.cpp" @ONLY)

add_library(joggle
  src/call_resolution.cpp
  src/declaration_check.cpp
  src/prelude.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/generated/prelude_source.cpp"
  src/compiler.cpp
  src/diagnostic.cpp
  src/domain.cpp
  src/equivalence.cpp
  src/execution.cpp
  src/execution_value.cpp
  src/expression_syntax.cpp
  src/ir.cpp
  src/module_ir.cpp
  src/function_body.cpp
  src/module.cpp
  src/module_repository.cpp
  src/sha256.cpp
  src/specialization.cpp
  src/syntax_lexer.cpp
  src/type.cpp
  src/type_contract.cpp
  src/transform.cpp
)
add_library(joggle::joggle ALIAS joggle)
set_target_properties(joggle PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)

add_executable(joggle_cli tools/joggle.cpp)
add_executable(joggle::joggle_cli ALIAS joggle_cli)
set_target_properties(joggle_cli PROPERTIES OUTPUT_NAME joggle)
if(APPLE)
  set_property(TARGET joggle_cli PROPERTY INSTALL_RPATH
    "@executable_path/../${CMAKE_INSTALL_LIBDIR}")
elseif(UNIX)
  set_property(TARGET joggle_cli PROPERTY INSTALL_RPATH
    "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()
target_link_libraries(joggle_cli PRIVATE joggle::joggle)
target_include_directories(joggle_cli PRIVATE src)

target_compile_features(joggle PUBLIC cxx_std_20)
target_include_directories(joggle PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(joggle PRIVATE ${CMAKE_DL_LIBS})

include(cmake/JoggleModule.cmake)

set(JOGGLE_SQUEEZENET_MODEL
  "${CMAKE_CURRENT_BINARY_DIR}/models/squeezenet1.1-7.onnx"
  CACHE FILEPATH "Build-cache path for the audited SqueezeNet model")
set(JOGGLE_ONNX_TEST_MODEL "" CACHE FILEPATH
  "Optional local path to the audited SqueezeNet model for ONNX tests")
add_custom_target(joggle_fetch_squeezenet
  COMMAND ${CMAKE_COMMAND}
    "-DJOGGLE_MODEL_OUTPUT=${JOGGLE_SQUEEZENET_MODEL}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchSqueezenet.cmake"
  COMMENT "Fetch the hash-pinned ONNX Model Zoo SqueezeNet model"
  VERBATIM)

if(JOGGLE_BUILD_ONNX)
  find_package(Protobuf REQUIRED)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(JOGGLE_PROTOBUF QUIET IMPORTED_TARGET protobuf)
  endif()
  set(JOGGLE_ONNX_PROTO
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/onnx/third_party/onnx.proto")
  file(SHA256 "${JOGGLE_ONNX_PROTO}" JOGGLE_ONNX_PROTO_SHA256)
  if(NOT JOGGLE_ONNX_PROTO_SHA256 STREQUAL
      "58f844f7b19b30bc38c68a081faef1f131d6858fd2166b061c6bbb98f75f5d0e")
    message(FATAL_ERROR
      "vendored ONNX schema hash does not match its audited provenance")
  endif()
  protobuf_generate_cpp(JOGGLE_ONNX_PROTO_SOURCES JOGGLE_ONNX_PROTO_HEADERS
    "${JOGGLE_ONNX_PROTO}")
  add_library(joggle_onnx_proto STATIC ${JOGGLE_ONNX_PROTO_SOURCES})
  set_target_properties(joggle_onnx_proto PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
  target_compile_features(joggle_onnx_proto PUBLIC cxx_std_20)
  target_include_directories(joggle_onnx_proto PUBLIC
    "${CMAKE_CURRENT_BINARY_DIR}")
  joggle_module(joggle_onnx_native
    SOURCE modules/onnx/module.joggle
    NATIVE
      modules/onnx/native.cpp
      modules/onnx/import.cpp)
  target_include_directories(joggle_onnx_native PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}")
  if(TARGET PkgConfig::JOGGLE_PROTOBUF)
    target_link_libraries(joggle_onnx_proto PUBLIC
      PkgConfig::JOGGLE_PROTOBUF)
  else()
    target_link_libraries(joggle_onnx_proto PUBLIC protobuf::libprotobuf)
  endif()
  target_link_libraries(joggle_onnx_native PRIVATE joggle_onnx_proto)
  if(MSVC)
    target_compile_options(joggle_onnx_native PRIVATE /W4 /WX)
    target_compile_options(joggle_onnx_proto PRIVATE /W0)
  else()
    target_compile_options(joggle_onnx_native PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    target_compile_options(joggle_onnx_proto PRIVATE -w)
  endif()
  set_target_properties(joggle_onnx_native PROPERTIES
    PREFIX ""
    OUTPUT_NAME "native"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/onnx"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/onnx")
  install(TARGETS joggle_onnx_native
    LIBRARY DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules/onnx
    RUNTIME DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules/onnx)
endif()

install(TARGETS joggle
  EXPORT JoggleTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(TARGETS joggle_cli
  EXPORT JoggleTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/joggle DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES language/prelude.joggle
  DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/language)
install(FILES modules/tensor/module.joggle
  DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules/tensor)
install(FILES modules/onnx/module.joggle
  DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules/onnx)
install(EXPORT JoggleTargets
  FILE JoggleTargets.cmake
  NAMESPACE joggle::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Joggle
)

configure_package_config_file(
  cmake/JoggleConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/JoggleConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Joggle
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/JoggleConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/JoggleConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/JoggleConfigVersion.cmake
  cmake/JoggleModule.cmake
  cmake/GenerateNativeIdentity.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Joggle
)

if(MSVC)
  target_compile_options(joggle PRIVATE /W4 /WX)
else()
  target_compile_options(joggle PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
  )
endif()

if(JOGGLE_BUILD_TESTS)
  enable_testing()
  set(JOGGLE_TEST_MODULE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/compiler_test.joggle")
  set(JOGGLE_TENSOR_MODULE
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/tensor/module.joggle")

  joggle_module(joggle_compiler_test_native
    SOURCE tests/fixtures/compiler_test.joggle
    NATIVE tests/compiler_test_native.cpp)
  set_target_properties(joggle_compiler_test_native PROPERTIES
    PREFIX ""
    OUTPUT_NAME "compiler_test_native"
  )

  add_executable(joggle_module_test tests/module_test.cpp)
  target_link_libraries(joggle_module_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_module_test PRIVATE
    JOGGLE_TEST_MODULE="${JOGGLE_TEST_MODULE}"
  )
  add_test(NAME module COMMAND joggle_module_test)

  add_executable(joggle_type_test tests/type_test.cpp)
  target_link_libraries(joggle_type_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_type_test PRIVATE
    JOGGLE_TEST_MODULE="${JOGGLE_TEST_MODULE}"
  )
  add_test(NAME type COMMAND joggle_type_test)

  add_executable(joggle_tensor_test tests/tensor_test.cpp)
  target_link_libraries(joggle_tensor_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_tensor_test PRIVATE
    JOGGLE_TENSOR_MODULE="${JOGGLE_TENSOR_MODULE}"
  )
  add_test(NAME tensor COMMAND joggle_tensor_test)

  add_executable(joggle_ir_test tests/ir_test.cpp)
  target_link_libraries(joggle_ir_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_ir_test PRIVATE
    JOGGLE_TEST_MODULE="${JOGGLE_TEST_MODULE}"
  )
  add_test(NAME ir COMMAND joggle_ir_test)

  add_executable(joggle_module_ir_test tests/module_ir_test.cpp)
  target_link_libraries(joggle_module_ir_test PRIVATE joggle::joggle)
  add_test(NAME module_ir COMMAND joggle_module_ir_test)

  add_executable(joggle_transform_test tests/transform_test.cpp)
  target_link_libraries(joggle_transform_test PRIVATE joggle::joggle)
  target_include_directories(joggle_transform_test PRIVATE src)
  add_test(NAME transform COMMAND joggle_transform_test)

  add_executable(joggle_equivalence_test tests/equivalence_test.cpp)
  target_link_libraries(joggle_equivalence_test PRIVATE joggle::joggle)
  add_test(NAME equivalence COMMAND joggle_equivalence_test)

  add_executable(joggle_specialization_test tests/specialization_test.cpp)
  target_link_libraries(joggle_specialization_test PRIVATE joggle::joggle)
  add_test(NAME specialization COMMAND joggle_specialization_test)

  add_executable(joggle_function_body_test tests/function_body_test.cpp)
  target_link_libraries(joggle_function_body_test PRIVATE joggle::joggle)
  add_test(NAME function_body COMMAND joggle_function_body_test)

  add_test(NAME function_cli
    COMMAND $<TARGET_FILE:joggle_cli> check
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/function_cli.joggle
      --with ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native_plugin.joggle
  )
  add_test(NAME function_run_cli
    COMMAND ${CMAKE_COMMAND}
      -DJOGGLE_CLI=$<TARGET_FILE:joggle_cli>
      -DJOGGLE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/function_cli.joggle
      -DJOGGLE_DEPENDENCY=${CMAKE_CURRENT_BINARY_DIR}/native_plugin.joggle
      -DJOGGLE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/function_run_output.bin
      -DJOGGLE_NATIVE=$<TARGET_FILE:joggle_test_native>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/function_run_cli.cmake
  )

  add_executable(joggle_binding_test tests/binding_test.cpp)
  target_link_libraries(joggle_binding_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_binding_test PRIVATE
    JOGGLE_TEST_MODULE="${JOGGLE_TEST_MODULE}"
  )
  add_test(NAME binding COMMAND joggle_binding_test)

  configure_file(tests/fixtures/native_plugin.joggle
    ${CMAKE_CURRENT_BINARY_DIR}/native_plugin.joggle COPYONLY)
  joggle_module(joggle_test_native
    SOURCE tests/fixtures/native_plugin.joggle
    NATIVE tests/native_plugin.cpp)
  set_target_properties(joggle_test_native PROPERTIES
    PREFIX ""
    OUTPUT_NAME "native"
  )
  joggle_module(joggle_test_native_failure
    SOURCE tests/fixtures/native_plugin.joggle
    NATIVE tests/native_plugin.cpp)
  target_compile_definitions(joggle_test_native_failure PRIVATE
    JOGGLE_TEST_NATIVE_FAIL=1)
  set_target_properties(joggle_test_native_failure PROPERTIES
    PREFIX ""
    OUTPUT_NAME "native_failure"
  )
  add_executable(joggle_native_loader_test tests/native_loader_test.cpp)
  target_link_libraries(joggle_native_loader_test PRIVATE joggle::joggle)
  add_test(NAME native_loader COMMAND joggle_native_loader_test
    ${CMAKE_CURRENT_BINARY_DIR}/native_plugin.joggle
    $<TARGET_FILE:joggle_test_native>
    $<TARGET_FILE:joggle_test_native_failure>
  )

  add_executable(joggle_prelude_test tests/prelude_test.cpp)
  target_link_libraries(joggle_prelude_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_prelude_test PRIVATE
    JOGGLE_PRELUDE_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/language/prelude.joggle")
  add_test(NAME prelude COMMAND joggle_prelude_test)

  add_executable(joggle_compiler_function_test tests/compiler_function_test.cpp)
  target_link_libraries(joggle_compiler_function_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_compiler_function_test PRIVATE
    JOGGLE_TEST_MODULE="${JOGGLE_TEST_MODULE}"
    JOGGLE_TEST_NATIVE="$<TARGET_FILE:joggle_compiler_test_native>"
  )
  add_test(NAME compiler_function COMMAND joggle_compiler_function_test)

  add_executable(joggle_sha256_test tests/sha256_test.cpp)
  target_link_libraries(joggle_sha256_test PRIVATE joggle::joggle)
  add_test(NAME sha256 COMMAND joggle_sha256_test)

  add_executable(joggle_module_repository_test
    tests/module_repository_test.cpp)
  target_link_libraries(joggle_module_repository_test PRIVATE joggle::joggle)
  target_include_directories(joggle_module_repository_test PRIVATE src)
  add_test(NAME module_repository COMMAND joggle_module_repository_test)

  add_test(NAME install_consumer
    COMMAND ${CMAKE_COMMAND}
      -DJOGGLE_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
      -DJOGGLE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/install_consumer.cmake
  )
  add_test(NAME install_consumer_static
    COMMAND ${CMAKE_COMMAND}
      -DJOGGLE_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-static-build
      -DJOGGLE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -DJOGGLE_INSTALL_CASE=static
      -DJOGGLE_CONFIGURE_STATIC=ON
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/install_consumer.cmake
  )

  add_executable(joggle_lock_consumer tests/lock_consumer.cpp)
  target_link_libraries(joggle_lock_consumer PRIVATE joggle::joggle)

  add_test(NAME module_repository_cli
    COMMAND ${CMAKE_COMMAND}
      -DJOGGLE_CLI=$<TARGET_FILE:joggle_cli>
      -DJOGGLE_REPOSITORY_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-module-repository
      -DJOGGLE_LOCK_CONSUMER=$<TARGET_FILE:joggle_lock_consumer>
      -DJOGGLE_NATIVE_MODULE=${CMAKE_CURRENT_BINARY_DIR}/native_plugin.joggle
      -DJOGGLE_NATIVE_LIBRARY=$<TARGET_FILE:joggle_test_native>
      -DJOGGLE_NATIVE_FAILURE=$<TARGET_FILE:joggle_test_native_failure>
      -DJOGGLE_NATIVE_LOADER=$<TARGET_FILE:joggle_native_loader_test>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/module_repository_cli.cmake
  )

  if(JOGGLE_BUILD_ONNX)
    add_executable(joggle_onnx_test tests/onnx_test.cpp)
    target_link_libraries(joggle_onnx_test PRIVATE joggle::joggle)
    add_test(NAME onnx
      COMMAND joggle_onnx_test
        ${JOGGLE_TENSOR_MODULE}
        ${CMAKE_CURRENT_SOURCE_DIR}/modules/onnx/module.joggle
        $<TARGET_FILE:joggle_onnx_native>
        ${JOGGLE_ONNX_TEST_MODEL})

    if(NOT JOGGLE_ONNX_TEST_MODEL STREQUAL "")
      add_test(NAME onnx_bundle_cli
        COMMAND ${CMAKE_COMMAND}
          -DJOGGLE_CLI=$<TARGET_FILE:joggle_cli>
          -DJOGGLE_TENSOR_MODULE=${JOGGLE_TENSOR_MODULE}
          -DJOGGLE_ONNX_MODULE=${CMAKE_CURRENT_SOURCE_DIR}/modules/onnx/module.joggle
          -DJOGGLE_ONNX_NATIVE=$<TARGET_FILE:joggle_onnx_native>
          -DJOGGLE_ONNX_MODEL=${JOGGLE_ONNX_TEST_MODEL}
          -DJOGGLE_BUNDLE_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/onnx-bundle-test
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/onnx_bundle_cli.cmake)
    endif()

    if(JOGGLE_ONNX_RUNTIME_VALIDATION)
      if(JOGGLE_ONNX_TEST_MODEL STREQUAL "")
        message(FATAL_ERROR
          "runtime validation requires JOGGLE_ONNX_TEST_MODEL")
      endif()
      find_package(Python3 REQUIRED COMPONENTS Interpreter)
      add_executable(joggle_onnx_roundtrip tests/onnx_roundtrip.cpp)
      target_link_libraries(joggle_onnx_roundtrip PRIVATE
        joggle::joggle joggle_onnx_proto)
      if(MSVC)
        target_compile_options(joggle_onnx_roundtrip PRIVATE /W4 /WX)
      else()
        target_compile_options(joggle_onnx_roundtrip PRIVATE
          -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
      endif()
      add_test(NAME onnx_runtime
        COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/tests/onnx_runtime_test.py
          --emitter $<TARGET_FILE:joggle_onnx_roundtrip>
          --tensor ${JOGGLE_TENSOR_MODULE}
          --onnx ${CMAKE_CURRENT_SOURCE_DIR}/modules/onnx/module.joggle
          --native $<TARGET_FILE:joggle_onnx_native>
          --model ${JOGGLE_ONNX_TEST_MODEL}
          --output ${CMAKE_CURRENT_BINARY_DIR}/squeezenet-roundtrip.onnx)
    endif()
  endif()
endif()
