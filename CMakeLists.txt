cmake_minimum_required(VERSION 3.20)

project(Joggle VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(JOGGLE_BUILD_TESTS "Build Joggle tests" ON)
option(JOGGLE_BUILD_ONNX "Build the optional ONNX module" OFF)
option(JOGGLE_BUILD_TFLITE "Build the optional TFLite module" OFF)
option(JOGGLE_BUILD_SAT "Build the optional saturating-integer module" OFF)
set(JOGGLE_TEST_ONNX_MODEL "" CACHE FILEPATH "Official ONNX model used by tests")
set(JOGGLE_TEST_TFLITE_MODEL "" CACHE FILEPATH
    "Official TFLite model used by tests")

foreach(module base ir math nn opt tensor)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/${module}")
  configure_file(
    "modules/${module}/module.jog"
    "${CMAKE_CURRENT_BINARY_DIR}/modules/${module}/module.jog"
    COPYONLY
  )
endforeach()

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

if(JOGGLE_BUILD_TFLITE)
  find_package(flatbuffers CONFIG REQUIRED)
  set(tflite_generated "${CMAKE_CURRENT_BINARY_DIR}/tflite-generated")
  file(MAKE_DIRECTORY "${tflite_generated}")
  add_custom_command(
    OUTPUT "${tflite_generated}/schema_generated.h"
    COMMAND $<TARGET_FILE:flatbuffers::flatc>
            --cpp --reflect-names
            -o "${tflite_generated}"
            "${CMAKE_CURRENT_SOURCE_DIR}/modules/tflite/src/schema.fbs"
    DEPENDS modules/tflite/src/schema.fbs flatbuffers::flatc
    VERBATIM
  )
  add_library(joggle-tflite MODULE
    modules/tflite/src/tflite.cpp
    "${tflite_generated}/schema_generated.h"
  )
  target_compile_features(joggle-tflite PRIVATE cxx_std_20)
  target_include_directories(
    joggle-tflite
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      "${tflite_generated}"
  )
  target_link_libraries(joggle-tflite PRIVATE flatbuffers::flatbuffers)
  if(MSVC)
    target_compile_options(joggle-tflite PRIVATE /W4)
  else()
    target_compile_options(joggle-tflite PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  set_target_properties(
    joggle-tflite
    PROPERTIES
      PREFIX ""
      OUTPUT_NAME "joggle_tflite"
      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/tflite/native"
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/tflite/native"
  )
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/tflite")
  configure_file(
    modules/tflite/module.jog
    "${CMAKE_CURRENT_BINARY_DIR}/modules/tflite/module.jog"
    COPYONLY
  )
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/tflite.nn")
  configure_file(
    modules/tflite.nn/module.jog
    "${CMAKE_CURRENT_BINARY_DIR}/modules/tflite.nn/module.jog"
    COPYONLY
  )
  install(
    TARGETS joggle-tflite
    LIBRARY DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules/tflite/native
    RUNTIME DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules/tflite/native
  )
  install(
    FILES modules/tflite/module.jog modules/tflite/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules/tflite
  )
  install(
    DIRECTORY modules/tflite.nn
    DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules
  )
endif()

if(JOGGLE_BUILD_ONNX)
  find_package(Protobuf CONFIG QUIET)
  if(NOT Protobuf_FOUND)
    find_package(Protobuf REQUIRED)
  endif()
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/onnx-generated")
  add_library(joggle-onnx-proto STATIC)
  target_link_libraries(joggle-onnx-proto PUBLIC protobuf::libprotobuf-lite)
  target_include_directories(
    joggle-onnx-proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/onnx-generated
  )
  set_target_properties(
    joggle-onnx-proto PROPERTIES POSITION_INDEPENDENT_CODE ON
  )
  protobuf_generate(
    TARGET joggle-onnx-proto
    LANGUAGE cpp
    PROTOS ${CMAKE_CURRENT_SOURCE_DIR}/modules/onnx/src/onnx.proto
    IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/modules/onnx/src
    PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/onnx-generated
    APPEND_PATH
  )
  add_library(joggle-onnx MODULE modules/onnx/src/onnx.cpp)
  target_compile_features(joggle-onnx PRIVATE cxx_std_20)
  target_include_directories(
    joggle-onnx
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
  target_link_libraries(joggle-onnx PRIVATE joggle-onnx-proto)
  if(MSVC)
    target_compile_options(joggle-onnx PRIVATE /W4)
  else()
    target_compile_options(joggle-onnx PRIVATE -Wall -Wextra -Wpedantic)
  endif()
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
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/onnx.nn")
  configure_file(
    modules/onnx.nn/module.jog
    "${CMAKE_CURRENT_BINARY_DIR}/modules/onnx.nn/module.jog"
    COPYONLY
  )
  install(
    TARGETS joggle-onnx
    LIBRARY DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules/onnx/native
    RUNTIME DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules/onnx/native
  )
  install(
    FILES modules/onnx/module.jog
    DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules/onnx
  )
  install(
    DIRECTORY modules/onnx.nn
    DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules
  )
endif()

if(JOGGLE_BUILD_SAT)
  add_library(joggle-sat MODULE modules/sat/src/sat.cpp)
  target_compile_features(joggle-sat PRIVATE cxx_std_20)
  target_include_directories(
    joggle-sat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
  if(MSVC)
    target_compile_options(joggle-sat PRIVATE /W4)
  else()
    target_compile_options(joggle-sat PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  set_target_properties(
    joggle-sat
    PROPERTIES
      PREFIX ""
      OUTPUT_NAME "joggle_sat"
      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/sat/native"
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/sat/native"
  )
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/sat")
  configure_file(
    modules/sat/module.jog
    "${CMAKE_CURRENT_BINARY_DIR}/modules/sat/module.jog"
    COPYONLY
  )
  install(
    TARGETS joggle-sat
    LIBRARY DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules/sat/native
    RUNTIME DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules/sat/native
  )
  install(
    FILES modules/sat/module.jog
    DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules/sat
  )
endif()

add_executable(joggle-tool tool/main.cpp tool/module.cpp)
set_target_properties(joggle-tool PROPERTIES OUTPUT_NAME joggle)
target_link_libraries(joggle-tool PRIVATE Joggle::joggle)

install(TARGETS joggle joggle-tool EXPORT JoggleTargets)
install(FILES include/joggle/joggle.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/joggle)
install(
  DIRECTORY
    modules/base
    modules/ir
    modules/math
    modules/nn
    modules/opt
    modules/tensor
  DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules
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
  add_executable(joggle-network-test test/network.cpp)
  target_link_libraries(joggle-network-test PRIVATE Joggle::joggle)
  add_test(
    NAME workflow
    COMMAND joggle-workflow-test
            ${CMAKE_CURRENT_SOURCE_DIR}/test/data/matmul.jog
            ${CMAKE_CURRENT_SOURCE_DIR}/modules
            ${CMAKE_CURRENT_BINARY_DIR}/modules
  )
  add_test(
    NAME network
    COMMAND joggle-network-test
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
  add_test(
    NAME cli-module
    COMMAND
      ${CMAKE_COMMAND}
      -DTOOL=$<TARGET_FILE:joggle-tool>
      -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/modules
      -DBUILD_ROOT=${CMAKE_CURRENT_BINARY_DIR}/modules
      -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/module-test
      -P ${CMAKE_CURRENT_SOURCE_DIR}/test/module.cmake
  )
  if(NOT CMAKE_CXX_FLAGS MATCHES "sanitize")
    add_test(
      NAME install-consumer
      COMMAND
        ${CMAKE_COMMAND}
        -DBUILD_ROOT=${CMAKE_CURRENT_BINARY_DIR}
        -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/consumer-test
        -DSUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
        -DCONFIG=$<CONFIG>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/install.cmake
    )
  endif()
  if(JOGGLE_BUILD_ONNX AND NOT JOGGLE_TEST_ONNX_MODEL STREQUAL "")
    file(SHA256 "${JOGGLE_TEST_ONNX_MODEL}" onnx_model_sha256)
    if(NOT onnx_model_sha256 STREQUAL
       "c1c513582d56afceff8516c73804e484c81c6a830712ab6d682253f4a3cd042f")
      message(FATAL_ERROR "JOGGLE_TEST_ONNX_MODEL is not the pinned model")
    endif()
    add_executable(joggle-onnx-test test/onnx.cpp)
    target_link_libraries(
      joggle-onnx-test PRIVATE Joggle::joggle joggle-onnx-proto
    )
    add_dependencies(joggle-onnx-test joggle-onnx)
    add_test(
      NAME onnx
      COMMAND joggle-onnx-test
              ${JOGGLE_TEST_ONNX_MODEL}
              ${CMAKE_CURRENT_BINARY_DIR}/modules
    )
  endif()
  if(JOGGLE_BUILD_TFLITE AND NOT JOGGLE_TEST_TFLITE_MODEL STREQUAL "")
    file(SHA256 "${JOGGLE_TEST_TFLITE_MODEL}" tflite_model_sha256)
    if(NOT tflite_model_sha256 STREQUAL
       "ff5cb7f9e62c92ebdad971f8a98aa6b3106d82a64587a7787c6a385c9e791339")
      message(FATAL_ERROR "JOGGLE_TEST_TFLITE_MODEL is not the pinned model")
    endif()
    add_executable(joggle-tflite-test test/tflite.cpp)
    target_link_libraries(joggle-tflite-test PRIVATE Joggle::joggle)
    add_dependencies(joggle-tflite-test joggle-tflite)
    add_test(
      NAME tflite
      COMMAND joggle-tflite-test
              ${JOGGLE_TEST_TFLITE_MODEL}
              ${CMAKE_CURRENT_BINARY_DIR}/modules
    )
  endif()
  if(JOGGLE_BUILD_SAT)
    add_executable(joggle-sat-test test/sat.cpp)
    target_link_libraries(joggle-sat-test PRIVATE Joggle::joggle)
    add_dependencies(joggle-sat-test joggle-sat)
    add_test(
      NAME sat
      COMMAND joggle-sat-test
              ${CMAKE_CURRENT_SOURCE_DIR}/test/data/sat.jog
              ${CMAKE_CURRENT_BINARY_DIR}/modules
    )
  endif()
endif()
