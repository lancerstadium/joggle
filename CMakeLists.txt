cmake_minimum_required(VERSION 3.20)

project(Joggle VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(JOGGLE_BUILD_TESTS "Build Joggle tests" ON)
option(JOGGLE_BUILD_ONNX "Build the optional ONNX module" OFF)
set(JOGGLE_TEST_ONNX_MODEL "" CACHE FILEPATH "Official ONNX model used by tests")

add_library(joggle src/env.cpp src/eval.cpp src/ir.cpp src/lang.cpp)
add_library(Joggle::joggle ALIAS joggle)

target_compile_features(joggle PUBLIC cxx_std_20)
target_link_libraries(joggle PRIVATE ${CMAKE_DL_LIBS})
target_include_directories(
  joggle
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
set_target_properties(joggle PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
  target_compile_options(joggle PRIVATE /W4)
else()
  target_compile_options(joggle PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(JOGGLE_BUILD_ONNX)
  find_package(Protobuf CONFIG QUIET)
  if(NOT Protobuf_FOUND)
    find_package(Protobuf REQUIRED)
  endif()
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/onnx-generated")
  add_library(joggle-onnx MODULE modules/onnx/src/onnx.cpp)
  target_compile_features(joggle-onnx PRIVATE cxx_std_20)
  target_include_directories(
    joggle-onnx
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_BINARY_DIR}/onnx-generated
  )
  target_link_libraries(joggle-onnx PRIVATE protobuf::libprotobuf-lite)
  if(MSVC)
    target_compile_options(joggle-onnx PRIVATE /W4)
  else()
    target_compile_options(joggle-onnx PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  protobuf_generate(
    TARGET joggle-onnx
    LANGUAGE cpp
    PROTOS ${CMAKE_CURRENT_SOURCE_DIR}/modules/onnx/src/onnx.proto
    IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/modules/onnx/src
    PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/onnx-generated
    APPEND_PATH
  )
  set_target_properties(
    joggle-onnx
    PROPERTIES
      PREFIX ""
      OUTPUT_NAME "joggle_onnx"
      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/onnx/native"
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/onnx/native"
  )
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/onnx")
  configure_file(
    modules/onnx/module.jog
    "${CMAKE_CURRENT_BINARY_DIR}/modules/onnx/module.jog"
    COPYONLY
  )
  install(
    TARGETS joggle-onnx
    LIBRARY DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules/onnx/native
    RUNTIME DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules/onnx/native
  )
endif()

add_executable(joggle-tool tool/main.cpp)
set_target_properties(joggle-tool PROPERTIES OUTPUT_NAME joggle)
target_link_libraries(joggle-tool PRIVATE Joggle::joggle)

install(TARGETS joggle joggle-tool EXPORT JoggleTargets)
install(FILES include/joggle/joggle.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/joggle)
install(
  DIRECTORY modules/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules
  PATTERN src EXCLUDE
  PATTERN .DS_Store EXCLUDE
)
install(
  EXPORT JoggleTargets
  FILE JoggleConfig.cmake
  NAMESPACE Joggle::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Joggle
)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/JoggleConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/JoggleConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Joggle
)

if(JOGGLE_BUILD_TESTS)
  enable_testing()
  add_library(joggle-test-module MODULE test/native.cpp)
  target_compile_features(joggle-test-module PRIVATE cxx_std_20)
  target_include_directories(
    joggle-test-module PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
  set_target_properties(
    joggle-test-module
    PROPERTIES
      PREFIX ""
      OUTPUT_NAME "joggle_sample"
      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/sample/native"
  )
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/sample")
  configure_file(
    test/data/sample.jog
    "${CMAKE_CURRENT_BINARY_DIR}/modules/sample/module.jog"
    COPYONLY
  )
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/bad")
  configure_file(
    test/data/bad/module.jog
    "${CMAKE_CURRENT_BINARY_DIR}/modules/bad/module.jog"
    COPYONLY
  )
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/script")
  configure_file(
    test/data/script/module.jog
    "${CMAKE_CURRENT_BINARY_DIR}/modules/script/module.jog"
    COPYONLY
  )
  add_executable(joggle-workflow-test test/workflow.cpp)
  target_link_libraries(joggle-workflow-test PRIVATE Joggle::joggle)
  add_test(
    NAME workflow
    COMMAND joggle-workflow-test
            ${CMAKE_CURRENT_SOURCE_DIR}/test/data/matmul.jog
            ${CMAKE_CURRENT_SOURCE_DIR}/modules
            ${CMAKE_CURRENT_BINARY_DIR}/modules
  )
  add_test(
    NAME cli-check
    COMMAND $<TARGET_FILE:joggle-tool> check
            ${CMAKE_CURRENT_SOURCE_DIR}/test/data/matmul.jog
  )
  add_test(
    NAME cli-run
    COMMAND $<TARGET_FILE:joggle-tool> run opt.fold_add_zero
            ${CMAKE_CURRENT_SOURCE_DIR}/test/data/matmul.jog
            -M ${CMAKE_CURRENT_SOURCE_DIR}/modules
  )
  set_tests_properties(
    cli-run PROPERTIES PASS_REGULAR_EXPRESSION "return x"
  )
  if(JOGGLE_BUILD_ONNX AND NOT JOGGLE_TEST_ONNX_MODEL STREQUAL "")
    file(SHA256 "${JOGGLE_TEST_ONNX_MODEL}" onnx_model_sha256)
    if(NOT onnx_model_sha256 STREQUAL
       "c1c513582d56afceff8516c73804e484c81c6a830712ab6d682253f4a3cd042f")
      message(FATAL_ERROR "JOGGLE_TEST_ONNX_MODEL is not the pinned model")
    endif()
    add_executable(joggle-onnx-test test/onnx.cpp)
    target_link_libraries(joggle-onnx-test PRIVATE Joggle::joggle)
    add_dependencies(joggle-onnx-test joggle-onnx)
    add_test(
      NAME onnx
      COMMAND joggle-onnx-test
              ${JOGGLE_TEST_ONNX_MODEL}
              ${CMAKE_CURRENT_BINARY_DIR}/modules
    )
  endif()
endif()
