cmake_minimum_required(VERSION 3.20)

project(Joggle VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(JOGGLE_BUILD_TESTS "Build Joggle tests" ON)
option(JOGGLE_BUILD_ONNX "Build the optional ONNX module" OFF)
option(JOGGLE_BUILD_TFLITE "Build the optional TFLite module" OFF)
option(JOGGLE_BUILD_SAT "Build the optional saturating-integer module" OFF)
option(JOGGLE_TEST_ONNX_HEAVY "Enable slow ONNX Model Zoo gates" OFF)
set(JOGGLE_TEST_ONNX_ZOO "" CACHE PATH
    "Directory containing the pinned ONNX Model Zoo test set")
set(JOGGLE_TEST_ONNX_BACKEND "" CACHE PATH
    "Directory containing official ONNX backend test cases")
set(JOGGLE_EXAMPLE_MNIST "" CACHE PATH
    "Extracted official MNIST ONNX application case")
set(JOGGLE_EXAMPLE_MOBILENET "" CACHE PATH
    "Extracted official MobileNetV2 ONNX application case")
set(JOGGLE_TEST_TFLITE_MODEL "" CACHE FILEPATH
    "Official TFLite model used by tests")

foreach(module base bounds c ir math mem nn opt quant stat tensor tile vm)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/${module}")
  configure_file(
    "modules/${module}/module.jog"
    "${CMAKE_CURRENT_BINARY_DIR}/modules/${module}/module.jog"
    COPYONLY
  )
endforeach()

add_library(joggle src/env.cpp src/eval.cpp src/ir.cpp src/lang.cpp)
add_library(Joggle::joggle ALIAS joggle)

add_library(joggle-vm MODULE modules/vm/src/vm.cpp)
target_compile_features(joggle-vm PRIVATE cxx_std_20)
target_include_directories(joggle-vm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(MSVC)
  target_compile_options(joggle-vm PRIVATE /W4)
else()
  target_compile_options(joggle-vm PRIVATE -Wall -Wextra -Wpedantic)
endif()
set_target_properties(
  joggle-vm
  PROPERTIES
    PREFIX ""
    OUTPUT_NAME "joggle_vm"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/vm/native"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/vm/native"
)

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
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(
      joggle-onnx-proto PRIVATE -Wno-nullability-extension
    )
  endif()
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
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/sat.c")
  configure_file(
    modules/sat.c/module.jog
    "${CMAKE_CURRENT_BINARY_DIR}/modules/sat.c/module.jog"
    COPYONLY
  )
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/sat.vm")
  configure_file(
    modules/sat.vm/module.jog
    "${CMAKE_CURRENT_BINARY_DIR}/modules/sat.vm/module.jog"
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
  install(
    DIRECTORY modules/sat.c modules/sat.vm
    DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules
  )
endif()

add_executable(joggle-tool tool/main.cpp tool/module.cpp)
set_target_properties(joggle-tool PROPERTIES OUTPUT_NAME joggle)
target_link_libraries(joggle-tool PRIVATE Joggle::joggle)

install(TARGETS joggle joggle-tool EXPORT JoggleTargets)
install(
  TARGETS joggle-vm
  LIBRARY DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules/vm/native
  RUNTIME DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules/vm/native
)
install(FILES include/joggle/joggle.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/joggle)
install(
  DIRECTORY
    modules/base
    modules/bounds
    modules/c
    modules/ir
    modules/math
    modules/mem
    modules/nn
    modules/opt
    modules/quant
    modules/stat
    modules/tensor
    modules/tile
  DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules
  PATTERN .DS_Store EXCLUDE
)
install(
  FILES modules/vm/module.jog
  DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules/vm
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
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/number")
  configure_file(
    test/data/number/module.jog
    "${CMAKE_CURRENT_BINARY_DIR}/modules/number/module.jog"
    COPYONLY
  )
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/tile_pass")
  configure_file(
    test/data/tile_pass/module.jog
    "${CMAKE_CURRENT_BINARY_DIR}/modules/tile_pass/module.jog"
    COPYONLY
  )
  add_executable(joggle-workflow-test test/workflow.cpp)
  target_link_libraries(joggle-workflow-test PRIVATE Joggle::joggle)
  add_executable(joggle-network-test test/network.cpp)
  target_link_libraries(joggle-network-test PRIVATE Joggle::joggle)
  add_executable(joggle-vm-test test/vm.cpp)
  target_link_libraries(joggle-vm-test PRIVATE Joggle::joggle)
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
    NAME vm
    COMMAND joggle-vm-test
            ${CMAKE_CURRENT_SOURCE_DIR}/test/data/vm.jog
            ${CMAKE_CURRENT_SOURCE_DIR}/test/data/c.jog
            ${CMAKE_CURRENT_SOURCE_DIR}/test/data/c_open.jog
            ${CMAKE_CURRENT_BINARY_DIR}/modules
  )
  add_test(
    NAME cli-check
    COMMAND $<TARGET_FILE:joggle-tool> check
            ${CMAKE_CURRENT_SOURCE_DIR}/test/data/matmul.jog
            -M ${CMAKE_CURRENT_SOURCE_DIR}/modules
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
    NAME cli-query
    COMMAND $<TARGET_FILE:joggle-tool> query opt.untyped
            ${CMAKE_CURRENT_SOURCE_DIR}/test/data/matmul.jog
            -M ${CMAKE_CURRENT_SOURCE_DIR}/modules
  )
  set_tests_properties(
    cli-query PROPERTIES PASS_REGULAR_EXPRESSION "\\[\\]"
  )
  add_test(
    NAME cli-args
    COMMAND
      ${CMAKE_COMMAND}
      -DTOOL=$<TARGET_FILE:joggle-tool>
      -DMODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/matmul.jog
      -DC_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/c.jog
      -DVM_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/vm.jog
      -DMODULES=${CMAKE_CURRENT_BINARY_DIR}/modules
      -P ${CMAKE_CURRENT_SOURCE_DIR}/test/args.cmake
  )
  add_test(
    NAME cost-example
    COMMAND $<TARGET_FILE:joggle-tool> query cost.total
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/cost/model.jog
            -M ${CMAKE_CURRENT_SOURCE_DIR}/examples
            -M ${CMAKE_CURRENT_BINARY_DIR}/modules
  )
  set_tests_properties(
    cost-example PROPERTIES PASS_REGULAR_EXPRESSION "5"
  )
  add_test(
    NAME cost-configured-example
    COMMAND $<TARGET_FILE:joggle-tool> query cost.total
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/cost/model.jog
            --arg 7 --arg 2
            -M ${CMAKE_CURRENT_SOURCE_DIR}/examples
            -M ${CMAKE_CURRENT_BINARY_DIR}/modules
  )
  set_tests_properties(
    cost-configured-example PROPERTIES PASS_REGULAR_EXPRESSION "9"
  )
  add_test(
    NAME stat-summary
    COMMAND
      ${CMAKE_COMMAND}
      -DTOOL=$<TARGET_FILE:joggle-tool>
      -DMODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/matmul.jog
      -DMODULES=${CMAKE_CURRENT_BINARY_DIR}/modules
      -P ${CMAKE_CURRENT_SOURCE_DIR}/test/stat.cmake
  )
  add_test(
    NAME bounds-report
    COMMAND
      ${CMAKE_COMMAND}
      -DTOOL=$<TARGET_FILE:joggle-tool>
      -DMODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/bounds.jog
      -DMODULES=${CMAKE_CURRENT_BINARY_DIR}/modules
      -P ${CMAKE_CURRENT_SOURCE_DIR}/test/bounds.cmake
  )
  add_test(
    NAME cli-emit
    COMMAND
      ${CMAKE_COMMAND}
      -DTOOL=$<TARGET_FILE:joggle-tool>
      -DMODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/matmul.jog
      -DSOURCE_MODULES=${CMAKE_CURRENT_SOURCE_DIR}/modules
      -DBUILD_MODULES=${CMAKE_CURRENT_BINARY_DIR}/modules
      -DOUT=${CMAKE_CURRENT_BINARY_DIR}/emit.bin
      -P ${CMAKE_CURRENT_SOURCE_DIR}/test/emit.cmake
  )
  find_program(JOGGLE_TEST_C_COMPILER NAMES cc clang gcc)
  if(JOGGLE_TEST_C_COMPILER)
    add_test(
      NAME ikj-example
      COMMAND
        ${CMAKE_COMMAND}
        -DTOOL=$<TARGET_FILE:joggle-tool>
        -DCC=${JOGGLE_TEST_C_COMPILER}
        -DMODEL=${CMAKE_CURRENT_SOURCE_DIR}/examples/ikj/model.jog
        -DHARNESS=${CMAKE_CURRENT_SOURCE_DIR}/examples/ikj/main.c
        -DMODULES=${CMAKE_CURRENT_BINARY_DIR}/modules
        -DEXAMPLES=${CMAKE_CURRENT_SOURCE_DIR}/examples
        -DROOT=${CMAKE_CURRENT_BINARY_DIR}/ikj-example
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/ikj.cmake
    )
    add_test(
      NAME edge-example
      COMMAND
        ${CMAKE_COMMAND}
        -DTOOL=$<TARGET_FILE:joggle-tool>
        -DCC=${JOGGLE_TEST_C_COMPILER}
        -DMODEL=${CMAKE_CURRENT_SOURCE_DIR}/examples/edge/model.jog
        -DKERNEL=${CMAKE_CURRENT_SOURCE_DIR}/examples/edge/kernel.c
        -DHARNESS=${CMAKE_CURRENT_SOURCE_DIR}/examples/edge/main.c
        -DMODULES=${CMAKE_CURRENT_BINARY_DIR}/modules
        -DEXAMPLES=${CMAKE_CURRENT_SOURCE_DIR}/examples
        -DROOT=${CMAKE_CURRENT_BINARY_DIR}/edge-example
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/edge.cmake
    )
    add_test(
      NAME c-execution
      COMMAND
        ${CMAKE_COMMAND}
        -DTOOL=$<TARGET_FILE:joggle-tool>
        -DCC=${JOGGLE_TEST_C_COMPILER}
        -DMODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/c.jog
        -DINVALID_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/c_invalid.jog
        -DOPEN_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/c_open.jog
        -DCOLLISION_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/c_collision.jog
        -DHARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/c_main.c
        -DBLOB_HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/c_blob_main.c
        -DOPEN_HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/c_open_main.c
        -DMODULES=${CMAKE_CURRENT_BINARY_DIR}/modules
        -DROOT=${CMAKE_CURRENT_BINARY_DIR}/c-test
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/c.cmake
    )
    add_test(
      NAME mem-execution
      COMMAND
        ${CMAKE_COMMAND}
        -DTOOL=$<TARGET_FILE:joggle-tool>
        -DCC=${JOGGLE_TEST_C_COMPILER}
        -DMODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/mem.jog
        -DHARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/mem_main.c
        -DMODULES=${CMAKE_CURRENT_BINARY_DIR}/modules
        -DROOT=${CMAKE_CURRENT_BINARY_DIR}/mem-test
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/mem.cmake
    )
    add_test(
      NAME tile-execution
      COMMAND
        ${CMAKE_COMMAND}
        -DTOOL=$<TARGET_FILE:joggle-tool>
        -DCC=${JOGGLE_TEST_C_COMPILER}
        -DMODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile.jog
        -DHARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile_main.c
        -DFUSE_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/mem.jog
        -DFUSE_HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/mem_main.c
        -DINVALID_FUSE_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile_invalid.jog
        -DEFFECT_FUSE_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile_effect.jog
        -DKEEP_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile_keep.jog
        -DKEEP_HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile_keep_main.c
        -DRELU_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile_relu.jog
        -DRELU_HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile_relu_main.c
        -DMODULES=${CMAKE_CURRENT_BINARY_DIR}/modules
        -DEXAMPLES=${CMAKE_CURRENT_SOURCE_DIR}/examples
        -DROOT=${CMAKE_CURRENT_BINARY_DIR}/tile-test
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/tile.cmake
    )
  endif()
  add_test(
    NAME cli-report
    COMMAND
      ${CMAKE_COMMAND}
      -DTOOL=$<TARGET_FILE:joggle-tool>
      -DMODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/matmul.jog
      -DSOURCE_MODULES=${CMAKE_CURRENT_SOURCE_DIR}/modules
      -DBUILD_MODULES=${CMAKE_CURRENT_BINARY_DIR}/modules
      -DOUT=${CMAKE_CURRENT_BINARY_DIR}/run-report.attr
      -P ${CMAKE_CURRENT_SOURCE_DIR}/test/report.cmake
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
  if(JOGGLE_BUILD_ONNX AND NOT JOGGLE_TEST_ONNX_ZOO STREQUAL "")
    set(onnx_zoo_names
      mnist-8
      mobilenetv2-7
      squeezenet1.1-7
      squeezenet1.0-13-qdq
      resnet18-v1-7
      tinyyolov2-8
      tiny-yolov3-11
      ultraface-rfb-320
      ssd-mobilenetv1-12
      shufflenet-v2-12
      densenet-12
      googlenet-12
      efficientnet-lite4-11-int8
      efficientnet-lite4-11-qdq
    )
    set(onnx_zoo_hashes
      2f06e72de813a8635c9bc0397ac447a601bdbfa7df4bebc278723b958831c9bf
      c1c513582d56afceff8516c73804e484c81c6a830712ab6d682253f4a3cd042f
      1eeff551a67ae8d565ca33b572fc4b66e3ef357b0eb2863bb9ff47a918cc4088
      4a567dd7542ef440890d57268fabf47211174c593d7a1837bd7f16a1067169e7
      4e8f8653e7a2222b3904cc3fe8e304cd8b339ce1d05fd24688162f86fb6df52c
      583fb7fdc948435ceac9fa82efc7708701efe8382a859a3dd46526b155f5f2ae
      f715cc2d99740d22d312777e20d9de2b2ecdc250155be8fd3752ce7e8b823521
      34cd7e60aeff28744c657de7a3dc64e872d506741de66987f3426f2b79f88017
      b8fba5e404077d4048d27fcd1667e85e27e192eb9bf51e696c46a3acd7d21058
      ea69821b4dd374ae2f33f9710dd1229ac263d0ee5b5a46ca3521f6483e1ba035
      0294e7e88e5b3360de9b0fdc321baf9e6ef18b7f058c4536caae3b9f18ed9ed5
      c99c507058eaf41de8723408fdda7db8325cb57f0a89f2ee07a716d6e963e14e
      2b3cbb5077262b20df565dacddecb3724c0976c35029a87e512d13aa4eff04a2
      6837d0b19625d4aff8266d7197a7f3775afd82a8c40f9fd0283d52db4955566f
    )
    if(JOGGLE_TEST_ONNX_HEAVY)
      list(APPEND onnx_zoo_names bidaf-9)
      list(APPEND onnx_zoo_hashes
        dfc317b56d065a3e297240a9e9b9118ff2260790b5850f4be2bc6ea1bcc65e80
      )
    endif()
    list(LENGTH onnx_zoo_names onnx_zoo_count)
    math(EXPR onnx_zoo_last "${onnx_zoo_count} - 1")
    foreach(index RANGE ${onnx_zoo_last})
      list(GET onnx_zoo_names ${index} name)
      list(GET onnx_zoo_hashes ${index} expected)
      set(model "${JOGGLE_TEST_ONNX_ZOO}/${name}.onnx")
      if(NOT EXISTS "${model}")
        message(FATAL_ERROR "missing pinned ONNX Model Zoo file: ${model}")
      endif()
      file(SHA256 "${model}" actual)
      if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "ONNX Model Zoo hash mismatch: ${model}")
      endif()
    endforeach()
    add_executable(joggle-onnx-test test/onnx.cpp)
    target_link_libraries(
      joggle-onnx-test PRIVATE Joggle::joggle joggle-onnx-proto
    )
    add_dependencies(joggle-onnx-test joggle-onnx)
    add_test(
      NAME onnx-zoo-mobilenet
      COMMAND joggle-onnx-test
              ${JOGGLE_TEST_ONNX_ZOO}/mobilenetv2-7.onnx
              ${CMAKE_CURRENT_BINARY_DIR}/modules
    )
    add_executable(joggle-onnx-zoo-test test/zoo.cpp)
    target_link_libraries(joggle-onnx-zoo-test PRIVATE Joggle::joggle)
    add_dependencies(joggle-onnx-zoo-test joggle-onnx)
    add_test(
      NAME onnx-zoo-mnist
      COMMAND joggle-onnx-zoo-test
              ${JOGGLE_TEST_ONNX_ZOO}/mnist-8.onnx
              ${CMAKE_CURRENT_BINARY_DIR}/modules
              onnx.Conv
              onnx.MaxPool
              onnx.Gemm
    )
    add_test(
      NAME onnx-zoo-squeezenet
      COMMAND joggle-onnx-zoo-test
              ${JOGGLE_TEST_ONNX_ZOO}/squeezenet1.1-7.onnx
              ${CMAKE_CURRENT_BINARY_DIR}/modules
              onnx.Concat
              onnx.Dropout
    )
    add_test(
      NAME onnx-zoo-squeezenet-qdq
      COMMAND joggle-onnx-zoo-test
              ${JOGGLE_TEST_ONNX_ZOO}/squeezenet1.0-13-qdq.onnx
              ${CMAKE_CURRENT_BINARY_DIR}/modules
              onnx.QuantizeLinear
              onnx.DequantizeLinear
    )
    add_test(
      NAME onnx-zoo-resnet
      COMMAND joggle-onnx-zoo-test
              ${JOGGLE_TEST_ONNX_ZOO}/resnet18-v1-7.onnx
              ${CMAKE_CURRENT_BINARY_DIR}/modules
              onnx.Add
              onnx.Gemm
    )
    add_test(
      NAME onnx-zoo-tinyyolo
      COMMAND joggle-onnx-zoo-test
              ${JOGGLE_TEST_ONNX_ZOO}/tinyyolov2-8.onnx
              ${CMAKE_CURRENT_BINARY_DIR}/modules
              onnx.LeakyRelu
              onnx.MaxPool
    )
    add_test(
      NAME onnx-zoo-tinyyolov3
      COMMAND joggle-onnx-zoo-test
              ${JOGGLE_TEST_ONNX_ZOO}/tiny-yolov3-11.onnx
              ${CMAKE_CURRENT_BINARY_DIR}/modules
              --frontier
              219
              onnx.Loop
    )
    add_test(
      NAME onnx-zoo-ultraface
      COMMAND joggle-onnx-zoo-test
              ${JOGGLE_TEST_ONNX_ZOO}/ultraface-rfb-320.onnx
              ${CMAKE_CURRENT_BINARY_DIR}/modules
              onnx.Constant
              onnx.Slice
              onnx.Softmax
    )
    add_test(
      NAME onnx-zoo-ssd-mobilenet
      COMMAND joggle-onnx-zoo-test
              ${JOGGLE_TEST_ONNX_ZOO}/ssd-mobilenetv1-12.onnx
              ${CMAKE_CURRENT_BINARY_DIR}/modules
              --frontier
              0
              onnx.Loop
              onnx.NonMaxSuppression
              onnx.Resize
    )
    add_test(
      NAME onnx-zoo-shufflenet
      COMMAND joggle-onnx-zoo-test
              ${JOGGLE_TEST_ONNX_ZOO}/shufflenet-v2-12.onnx
              ${CMAKE_CURRENT_BINARY_DIR}/modules
              onnx.Split
              onnx.Concat
    )
    add_test(
      NAME onnx-zoo-densenet
      COMMAND joggle-onnx-zoo-test
              ${JOGGLE_TEST_ONNX_ZOO}/densenet-12.onnx
              ${CMAKE_CURRENT_BINARY_DIR}/modules
              --frontier-from-signature
              0
              onnx.Concat
              onnx.BatchNormalization
    )
    add_test(
      NAME onnx-zoo-googlenet
      COMMAND joggle-onnx-zoo-test
              ${JOGGLE_TEST_ONNX_ZOO}/googlenet-12.onnx
              ${CMAKE_CURRENT_BINARY_DIR}/modules
              --frontier-from-signature
              0
              onnx.Concat
              onnx.LRN
              onnx.Dropout
    )
    add_test(
      NAME onnx-zoo-efficientnet-qdq
      COMMAND joggle-onnx-zoo-test
              ${JOGGLE_TEST_ONNX_ZOO}/efficientnet-lite4-11-qdq.onnx
              ${CMAKE_CURRENT_BINARY_DIR}/modules
              --frontier-from-signature
              0
              onnx.QuantizeLinear
              onnx.DequantizeLinear
    )
    add_test(
      NAME onnx-zoo-efficientnet-int8
      COMMAND joggle-onnx-zoo-test
              ${JOGGLE_TEST_ONNX_ZOO}/efficientnet-lite4-11-int8.onnx
              ${CMAKE_CURRENT_BINARY_DIR}/modules
              --frontier-from-signature
              95
              onnx.QLinearConv
              onnx.QLinearMatMul
    )
    if(JOGGLE_TEST_ONNX_HEAVY)
      add_test(
        NAME onnx-zoo-bidaf
        COMMAND joggle-onnx-zoo-test
                ${JOGGLE_TEST_ONNX_ZOO}/bidaf-9.onnx
                ${CMAKE_CURRENT_BINARY_DIR}/modules
                --roundtrip
                onnx.Gather
      )
      set_tests_properties(
        onnx-zoo-bidaf PROPERTIES LABELS "heavy;onnx-zoo" TIMEOUT 900
      )
    endif()
  endif()
  if(JOGGLE_BUILD_ONNX)
    add_executable(joggle-onnx-codec-test test/onnx_codec.cpp)
    target_link_libraries(
      joggle-onnx-codec-test PRIVATE Joggle::joggle joggle-onnx-proto
    )
    add_dependencies(joggle-onnx-codec-test joggle-onnx)
    add_test(
      NAME onnx-codec
      COMMAND joggle-onnx-codec-test ${CMAKE_CURRENT_BINARY_DIR}/modules
    )
  endif()
  if(JOGGLE_BUILD_ONNX AND JOGGLE_TEST_C_COMPILER AND
     NOT JOGGLE_TEST_ONNX_BACKEND STREQUAL "")
    set(onnx_execute_case "${JOGGLE_TEST_ONNX_BACKEND}/test_matmul_2d")
    set(onnx_execute_files
      model.onnx
      test_data_set_0/input_0.pb
      test_data_set_0/input_1.pb
      test_data_set_0/output_0.pb
    )
    set(onnx_execute_hashes
      946f3a49ccfdaf62586bbae8438065fe41a7e6a9b8006b14af3219999b49bb76
      5c9fa13b29d31b4105ea162c0caecc951804499b016bdf268b8f470b404c1f79
      7d950d6d6af29dfa3acab8c9fcdea76d553c11124013b106eb6622bc0ebf3dd4
      c7928ba3e8fe85076974c955200532c2a24697119eb54b338fbc7c2639228e48
    )
    foreach(index RANGE 3)
      list(GET onnx_execute_files ${index} file)
      list(GET onnx_execute_hashes ${index} expected)
      if(NOT EXISTS "${onnx_execute_case}/${file}")
        message(FATAL_ERROR "missing pinned ONNX backend file: ${file}")
      endif()
      file(SHA256 "${onnx_execute_case}/${file}" actual)
      if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "ONNX backend hash mismatch: ${file}")
      endif()
    endforeach()
    add_executable(joggle-onnx-execute-test test/onnx_execute.cpp)
    target_link_libraries(
      joggle-onnx-execute-test PRIVATE Joggle::joggle joggle-onnx-proto
    )
    add_dependencies(joggle-onnx-execute-test joggle-onnx joggle-vm)
    add_test(
      NAME onnx-execution
      COMMAND
        ${CMAKE_COMMAND}
        -DTEST=$<TARGET_FILE:joggle-onnx-execute-test>
        -DCC=${JOGGLE_TEST_C_COMPILER}
        -DCASE=${onnx_execute_case}
        -DMODULES=${CMAKE_CURRENT_BINARY_DIR}/modules
        -DEXAMPLES=${CMAKE_CURRENT_SOURCE_DIR}/examples
        -DROOT=${CMAKE_CURRENT_BINARY_DIR}/onnx-execution
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/onnx_execute.cmake
    )
    set_tests_properties(onnx-execution PROPERTIES LABELS "onnx;execution")
    if(APPLE AND CMAKE_CXX_FLAGS MATCHES "sanitize=address")
      # Homebrew protobuf-lite is not ASan-instrumented, while Apple's libc++
      # annotates std::string capacity in instrumented callers. Disable only
      # those container annotations for this mixed-ABI fixture parser; normal
      # address and undefined-behavior checks remain active.
      set_property(
        TEST onnx-execution PROPERTY ENVIRONMENT
        "ASAN_OPTIONS=detect_container_overflow=0"
      )
    endif()
  endif()
  if(JOGGLE_BUILD_ONNX AND JOGGLE_TEST_C_COMPILER AND
     (NOT JOGGLE_EXAMPLE_MNIST STREQUAL "" OR
      NOT JOGGLE_EXAMPLE_MOBILENET STREQUAL ""))
    add_executable(joggle-onnx-app examples/onnx/app.cpp)
    target_link_libraries(
      joggle-onnx-app PRIVATE Joggle::joggle joggle-onnx-proto
    )
    add_dependencies(joggle-onnx-app joggle-onnx joggle-vm)

    function(joggle_onnx_app name case model model_hash input_hash output_hash
             timeout labels)
      if("${case}" STREQUAL "")
        return()
      endif()
      set(files "${model}" test_data_set_0/input_0.pb
                test_data_set_0/output_0.pb)
      set(hashes "${model_hash}" "${input_hash}" "${output_hash}")
      foreach(index RANGE 2)
        list(GET files ${index} file)
        list(GET hashes ${index} expected)
        if(NOT EXISTS "${case}/${file}")
          message(FATAL_ERROR "missing pinned ONNX application file: ${file}")
        endif()
        file(SHA256 "${case}/${file}" actual)
        if(NOT actual STREQUAL expected)
          message(FATAL_ERROR "ONNX application hash mismatch: ${file}")
        endif()
      endforeach()
      add_test(
        NAME onnx-app-${name}
        COMMAND
          ${CMAKE_COMMAND}
          -DAPP=$<TARGET_FILE:joggle-onnx-app>
          -DTOOL=$<TARGET_FILE:joggle-tool>
          -DCC=${JOGGLE_TEST_C_COMPILER}
          -DMODEL=${case}/${model}
          -DINPUT=${case}/test_data_set_0/input_0.pb
          -DOUTPUT=${case}/test_data_set_0/output_0.pb
          -DMODULES=${CMAKE_CURRENT_BINARY_DIR}/modules
          -DHARNESS=${CMAKE_CURRENT_SOURCE_DIR}/examples/onnx/main.c
          -DBENCHMARK=${CMAKE_CURRENT_SOURCE_DIR}/examples/onnx/benchmark.c
          -DROOT=${CMAKE_CURRENT_BINARY_DIR}/examples/${name}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/examples/onnx/run.cmake
      )
      set_tests_properties(
        onnx-app-${name} PROPERTIES LABELS "${labels}" TIMEOUT ${timeout}
      )
      if(APPLE AND CMAKE_CXX_FLAGS MATCHES "sanitize=address")
        # The official TensorProto fixture is parsed by Homebrew protobuf-lite,
        # whose uninstrumented std::string storage conflicts with libc++'s
        # ASan container annotations. Keep all other sanitizer checks enabled.
        set_property(
          TEST onnx-app-${name} PROPERTY ENVIRONMENT
          "ASAN_OPTIONS=detect_container_overflow=0"
        )
      endif()
    endfunction()

    joggle_onnx_app(
      mnist "${JOGGLE_EXAMPLE_MNIST}" model.onnx
      2f06e72de813a8635c9bc0397ac447a601bdbfa7df4bebc278723b958831c9bf
      2b4cf8da91bc87fbe0c3e52e54ea1c48d9744f5c6139d3d7457927506ab9c286
      6d0aea7dae241e2e09f75f3552f535026afa58f353a1ca58590db48f4e364c62
      120 "onnx;execution"
    )
    joggle_onnx_app(
      mobilenet "${JOGGLE_EXAMPLE_MOBILENET}" mobilenetv2-7.onnx
      c1c513582d56afceff8516c73804e484c81c6a830712ab6d682253f4a3cd042f
      8d2dd7bc0a81404c443315a7cdcec81b502082cf3f450cec204b7278be85a254
      55c3689e6946111fd78ad250e857c36d58fd383819dd0d3997cd1332c7d86cbd
      1200 "heavy;onnx;execution"
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
  if(JOGGLE_BUILD_TFLITE)
    add_executable(joggle-tflite-value-test test/tflite_value.cpp)
    target_include_directories(
      joggle-tflite-value-test
      PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/tflite-generated
    )
    target_link_libraries(
      joggle-tflite-value-test PRIVATE Joggle::joggle flatbuffers::flatbuffers
    )
    add_dependencies(joggle-tflite-value-test joggle-tflite)
    add_test(
      NAME tflite-value
      COMMAND joggle-tflite-value-test ${CMAKE_CURRENT_BINARY_DIR}/modules
    )
  endif()
  if(JOGGLE_BUILD_SAT)
    add_executable(joggle-sat-test test/sat.cpp)
    target_link_libraries(joggle-sat-test PRIVATE Joggle::joggle)
    add_dependencies(joggle-sat-test joggle-sat joggle-vm)
    add_test(
      NAME sat
      COMMAND joggle-sat-test
              ${CMAKE_CURRENT_SOURCE_DIR}/test/data/sat.jog
              ${CMAKE_CURRENT_SOURCE_DIR}/test/data/sat_vm.jog
              ${CMAKE_CURRENT_BINARY_DIR}/modules
    )
    if(JOGGLE_TEST_C_COMPILER)
      add_test(
        NAME sat-c-execution
        COMMAND
          ${CMAKE_COMMAND}
          -DTOOL=$<TARGET_FILE:joggle-tool>
          -DCC=${JOGGLE_TEST_C_COMPILER}
          -DMODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/sat_c.jog
          -DHARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/sat_c_main.c
          -DMODULES=${CMAKE_CURRENT_BINARY_DIR}/modules
          -DROOT=${CMAKE_CURRENT_BINARY_DIR}/sat-c
          -P ${CMAKE_CURRENT_SOURCE_DIR}/test/sat_c.cmake
      )
    endif()
  endif()
endif()
