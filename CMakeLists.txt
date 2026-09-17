cmake_minimum_required(VERSION 3.20)

project(Joggle VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(JOGGLE_BUILD_TESTS "Build Joggle tests" ON)
option(JOGGLE_BUILD_ONNX "Build the optional ONNX module" OFF)
option(JOGGLE_BUILD_TFLITE "Build the optional TFLite module" OFF)
option(JOGGLE_BUILD_SAT "Build the optional saturating-integer module" OFF)
option(JOGGLE_TEST_ONNX_HEAVY "Enable slow ONNX Model Zoo gates" OFF)
# The paper/ research workspace is not part of the shipped project, so nothing
# here may read it by default: a clone must build and test without it. The
# research targets and record checks are registered only when this is ON.
option(JOGGLE_RESEARCH
       "Register tools and checks that read the paper/ research workspace" OFF)
set(JOGGLE_TEST_ONNX_ZOO "" CACHE PATH
    "Directory containing any subset of the pinned ONNX Model Zoo test set")
set(JOGGLE_TEST_ONNX_BACKEND "" CACHE PATH
    "Directory containing official ONNX backend test cases")
set(JOGGLE_EXAMPLE_MNIST "" CACHE PATH
    "Extracted official MNIST ONNX application case")
set(JOGGLE_EXAMPLE_MOBILENET "" CACHE PATH
    "Extracted official MobileNetV2 ONNX application case")
set(JOGGLE_TEST_TFLITE_MODEL "" CACHE FILEPATH
    "Official TFLite model used by tests")

function(joggle_copy_source_module module)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/${module}")
  configure_file(
    "modules/${module}/module.jog"
    "${CMAKE_CURRENT_BINARY_DIR}/modules/${module}/module.jog"
    COPYONLY
  )
  file(GLOB module_fragments CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/modules/${module}/lib/*.jog")
  foreach(fragment IN LISTS module_fragments)
    get_filename_component(fragment_name "${fragment}" NAME)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/${module}/lib")
    configure_file(
      "${fragment}"
      "${CMAKE_CURRENT_BINARY_DIR}/modules/${module}/lib/${fragment_name}"
      COPYONLY
    )
  endforeach()
endfunction()

foreach(module base bounds c ir math mem nn opt quant stat tensor tile vm)
  joggle_copy_source_module(${module})
endforeach()

# joggle_script_test(<name> <script.cmake>
#                    [DEFINES <VAR=value>...] [LABELS <label>...]
#                    [TIMEOUT <seconds>])
#
# Registers one script-driven test. TOOL and MODULES are supplied here because
# every such test needs them and spelling them out 29 times let them drift.
# DEFINES follow the defaults, so a test that needs a different MODULES root can
# still override it.
function(joggle_script_test name script)
  cmake_parse_arguments(PARSE_ARGV 2 arg "" "TIMEOUT" "DEFINES;LABELS")
  set(defines "")
  foreach(entry IN LISTS arg_DEFINES)
    list(APPEND defines "-D${entry}")
  endforeach()
  add_test(
    NAME ${name}
    COMMAND
      ${CMAKE_COMMAND}
      -DTOOL=$<TARGET_FILE:joggle-tool>
      -DMODULES=${CMAKE_CURRENT_BINARY_DIR}/modules
      ${defines}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/test/${script}
  )
  set(properties "")
  if(arg_LABELS)
    list(APPEND properties LABELS "${arg_LABELS}")
  endif()
  if(arg_TIMEOUT)
    list(APPEND properties TIMEOUT "${arg_TIMEOUT}")
  endif()
  if(properties)
    set_tests_properties(${name} PROPERTIES ${properties})
  endif()
endfunction()

# joggle_label(<label> <test>...)
#
# Adds a kind label to tests that exist, preserving labels already set.
#
# Tests are registered in several blocks, some conditional on an option or on a
# pinned model being present, so the taxonomy is assigned once at the end of the
# test section rather than at each of the fifty registration sites. A name that
# is absent is skipped instead of raising an error, which is what lets a group
# list a test whose option is currently off.
function(joggle_label label)
  foreach(name IN LISTS ARGN)
    if(NOT TEST ${name})
      continue()
    endif()
    get_test_property(${name} LABELS current)
    set(merged "${label}")
    if(current)
      list(APPEND merged ${current})
    endif()
    list(REMOVE_DUPLICATES merged)
    set_tests_properties(${name} PROPERTIES LABELS "${merged}")
  endforeach()
endfunction()

add_library(joggle
  src/env.cpp
  src/eval.cpp
  src/handle.cpp
  src/ir.cpp
  src/lang.cpp
  src/language.cpp
  src/lex.cpp
  src/meta.cpp
  src/print.cpp
  src/type.cpp
  src/value.cpp
  src/verify.cpp
)
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
  find_package(Flatbuffers CONFIG QUIET)
  if(NOT TARGET flatbuffers::flatbuffers)
    find_package(flatbuffers CONFIG QUIET)
  endif()
  if(NOT TARGET flatbuffers::flatbuffers)
    find_package(FlatBuffers CONFIG REQUIRED)
  endif()
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
  joggle_copy_source_module(tflite)
  joggle_copy_source_module(tflite.nn)
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
  joggle_copy_source_module(onnx)
  joggle_copy_source_module(onnx.nn)
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
  joggle_copy_source_module(sat)
  joggle_copy_source_module(sat.c)
  joggle_copy_source_module(sat.vm)
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

add_executable(joggle-tool tool/diag.cpp tool/main.cpp tool/module.cpp)
set_target_properties(joggle-tool PROPERTIES OUTPUT_NAME joggle)
target_link_libraries(joggle-tool PRIVATE Joggle::joggle)
target_compile_definitions(
  joggle-tool PRIVATE JOGGLE_VERSION="${PROJECT_VERSION}"
)

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
  add_test(NAME cli-version COMMAND joggle-tool --version)
  set_tests_properties(
    cli-version PROPERTIES PASS_REGULAR_EXPRESSION "^joggle ${PROJECT_VERSION}"
  )
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
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules/alternate")
  configure_file(
    test/data/alternate/module.jog
    "${CMAKE_CURRENT_BINARY_DIR}/modules/alternate/module.jog"
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
  file(
    COPY test/data/resolve/
    DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/modules"
  )
  add_executable(joggle-workflow-test test/workflow.cpp)
  target_link_libraries(joggle-workflow-test PRIVATE Joggle::joggle)
  if(JOGGLE_RESEARCH)
    add_executable(joggle-derive-prepare EXCLUDE_FROM_ALL paper/experiments/derive_prepare.cpp)
    target_link_libraries(joggle-derive-prepare PRIVATE Joggle::joggle)
    add_executable(joggle-progressive-trace EXCLUDE_FROM_ALL paper/experiments/progressive_trace.cpp)
    target_link_libraries(joggle-progressive-trace PRIVATE Joggle::joggle)
  endif()
  add_executable(joggle-network-test test/network.cpp)
  target_link_libraries(joggle-network-test PRIVATE Joggle::joggle)
  add_executable(joggle-lang-robustness-test test/lang_robustness.cpp)
  target_link_libraries(joggle-lang-robustness-test PRIVATE Joggle::joggle)
  add_executable(joggle-edit-robustness-test test/edit_robustness.cpp)
  target_link_libraries(joggle-edit-robustness-test PRIVATE Joggle::joggle)
  add_executable(joggle-module-robustness-test test/module_robustness.cpp)
  target_link_libraries(joggle-module-robustness-test PRIVATE Joggle::joggle)
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
  add_test(NAME lang-robustness COMMAND joggle-lang-robustness-test)
  add_test(
    NAME edit-robustness
    COMMAND joggle-edit-robustness-test
            ${CMAKE_CURRENT_SOURCE_DIR}/modules
  )
  add_test(
    NAME module-robustness
    COMMAND joggle-module-robustness-test
            ${CMAKE_CURRENT_BINARY_DIR}/module-robustness
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
  joggle_script_test(cli-args args.cmake
    DEFINES
      MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/matmul.jog
      C_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/c.jog
      VM_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/vm.jog
  )
  joggle_script_test(cli-stream stream.cmake
    DEFINES
      MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/matmul.jog
      ROOT=${CMAKE_CURRENT_BINARY_DIR}/stream-test
  )
  joggle_script_test(cli-diagnostics diagnostics.cmake
    DEFINES
      MODEL=${CMAKE_CURRENT_BINARY_DIR}/modules/bad/module.jog
  )
  add_test(
    NAME cost-extension
    COMMAND $<TARGET_FILE:joggle-tool> query cost.total
            ${CMAKE_CURRENT_SOURCE_DIR}/extensions/cost/model.jog
            -M ${CMAKE_CURRENT_SOURCE_DIR}/extensions
            -M ${CMAKE_CURRENT_BINARY_DIR}/modules
  )
  set_tests_properties(
    cost-extension PROPERTIES PASS_REGULAR_EXPRESSION "5"
  )
  add_test(
    NAME cost-configured-extension
    COMMAND $<TARGET_FILE:joggle-tool> query cost.total
            ${CMAKE_CURRENT_SOURCE_DIR}/extensions/cost/model.jog
            --arg 7 --arg 2
            -M ${CMAKE_CURRENT_SOURCE_DIR}/extensions
            -M ${CMAKE_CURRENT_BINARY_DIR}/modules
  )
  set_tests_properties(
    cost-configured-extension PROPERTIES PASS_REGULAR_EXPRESSION "9"
  )
  joggle_script_test(stat-summary stat.cmake
    DEFINES
      MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/matmul.jog
  )
  joggle_script_test(bounds-report bounds.cmake
    DEFINES
      MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/bounds.jog
      FOLD_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/bounds_resolved.jog
      CUSTOM_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/bounds_custom.jog
      ROOT=${CMAKE_CURRENT_BINARY_DIR}/bounds-report
  )
  joggle_script_test(opt-overloads opt.cmake
    DEFINES
      MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/opt_custom.jog
      CSE_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/opt_cse.jog
  )
  joggle_script_test(cli-emit emit.cmake
    DEFINES
      MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/matmul.jog
      SOURCE_MODULES=${CMAKE_CURRENT_SOURCE_DIR}/modules
      BUILD_MODULES=${CMAKE_CURRENT_BINARY_DIR}/modules
      OUT=${CMAKE_CURRENT_BINARY_DIR}/emit.bin
  )
  find_program(JOGGLE_TEST_C_COMPILER NAMES cc clang gcc)
  find_program(JOGGLE_TEST_PYTHON NAMES python3 python)
  # Every check below reads the paper/ research workspace, so all of them are
  # gated. They verify recorded evidence and baseline records, which is research
  # work rather than product coverage.
  if(JOGGLE_TEST_PYTHON AND JOGGLE_RESEARCH)
    add_test(
      NAME paper-fixtures
      COMMAND
        ${JOGGLE_TEST_PYTHON}
        ${CMAKE_CURRENT_SOURCE_DIR}/paper/fixtures/check.py
    )
    add_test(
      NAME onnx-mlir-policy-record
      COMMAND
        ${JOGGLE_TEST_PYTHON}
        ${CMAKE_CURRENT_SOURCE_DIR}/paper/baselines/onnx-mlir/policy/check.py
    )
    add_test(
      NAME onnx-mlir-numeric-format-record
      COMMAND
        ${JOGGLE_TEST_PYTHON}
        ${CMAKE_CURRENT_SOURCE_DIR}/paper/baselines/onnx-mlir/numeric-format/check.py
    )
    # The two matched-baseline extension tables are generated from the preserved
    # per-task records by one collector, so a table can no longer drift from the
    # records it claims to summarise, and a task a system cannot complete stays a
    # row rather than becoming a missing row.
    foreach(system tvm onnx-mlir)
      add_test(
        NAME baseline-table-${system}
        COMMAND
          ${JOGGLE_TEST_PYTHON}
          ${CMAKE_CURRENT_SOURCE_DIR}/paper/scripts/collect_baselines.py
          ${system} --check
      )
      set_tests_properties(
        baseline-table-${system} PROPERTIES LABELS "research"
      )
    endforeach()
    add_test(
      NAME measure-systems
      COMMAND
        ${JOGGLE_TEST_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/test/measure_systems.py
        --runner ${CMAKE_CURRENT_SOURCE_DIR}/paper/scripts/measure_systems.py
        --repo ${CMAKE_CURRENT_SOURCE_DIR}
        --root ${CMAKE_CURRENT_BINARY_DIR}/measure-systems
    )
  endif()
  # Checks the layout rules stated in README.md. This needs only CMake, so it
  # runs whether or not Python was found. See test/layout.cmake for the enforce
  # and report classes: a report-class finding is listed and the test passes,
  # so the list is the migration queue rather than a red suite.
  joggle_script_test(layout layout.cmake
    DEFINES
      SOURCE=${CMAKE_CURRENT_SOURCE_DIR}
    LABELS lint
  )
  if(JOGGLE_TEST_C_COMPILER)
    joggle_script_test(ikj-extension ikj.cmake
      DEFINES
        CC=${JOGGLE_TEST_C_COMPILER}
        MODEL=${CMAKE_CURRENT_SOURCE_DIR}/extensions/ikj/model.jog
        HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/extensions/ikj/main.c
        EXTENSIONS=${CMAKE_CURRENT_SOURCE_DIR}/extensions
        ROOT=${CMAKE_CURRENT_BINARY_DIR}/ikj-extension
    )
    joggle_script_test(edge-extension edge.cmake
      DEFINES
        CC=${JOGGLE_TEST_C_COMPILER}
        MODEL=${CMAKE_CURRENT_SOURCE_DIR}/extensions/edge/model.jog
        KERNEL=${CMAKE_CURRENT_SOURCE_DIR}/extensions/edge/kernel.c
        HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/extensions/edge/main.c
        EXTENSIONS=${CMAKE_CURRENT_SOURCE_DIR}/extensions
        ROOT=${CMAKE_CURRENT_BINARY_DIR}/edge-extension
    )
    joggle_script_test(locality-extension locality.cmake
      DEFINES
        CC=${JOGGLE_TEST_C_COMPILER}
        MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/spatial.jog
        HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/spatial_main.c
        EXTENSIONS=${CMAKE_CURRENT_SOURCE_DIR}/extensions
        ROOT=${CMAKE_CURRENT_BINARY_DIR}/locality-extension
    )
    joggle_script_test(compact-extension compact.cmake
      DEFINES
        CC=${JOGGLE_TEST_C_COMPILER}
        MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/spatial.jog
        HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/spatial_main.c
        EXTENSIONS=${CMAKE_CURRENT_SOURCE_DIR}/extensions
        ROOT=${CMAKE_CURRENT_BINARY_DIR}/compact-extension
    )
    joggle_script_test(c-execution c.cmake
      DEFINES
        CC=${JOGGLE_TEST_C_COMPILER}
        MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/c.jog
        INVALID_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/c_invalid.jog
        OPEN_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/c_open.jog
        RESTRICT_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/c_restrict.jog
        COLLISION_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/c_collision.jog
        HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/c_main.c
        BLOB_HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/c_blob_main.c
        OPEN_HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/c_open_main.c
        PYTHON=${JOGGLE_TEST_PYTHON}
        GENERATOR=${CMAKE_CURRENT_SOURCE_DIR}/test/tools/make_harness.py
        ROOT=${CMAKE_CURRENT_BINARY_DIR}/c-test
    )
    joggle_script_test(quant-c-execution quant_c.cmake
      DEFINES
        CC=${JOGGLE_TEST_C_COMPILER}
        MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/quant_c.jog
        HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/quant_c_main.c
        ROOT=${CMAKE_CURRENT_BINARY_DIR}/quant-c-test
    )
    if(JOGGLE_BUILD_ONNX)
      joggle_script_test(qlinear-c-execution qlinear_c.cmake
        DEFINES
          CC=${JOGGLE_TEST_C_COMPILER}
          MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/qlinear_c.jog
          HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/qlinear_c_main.c
          ROOT=${CMAKE_CURRENT_BINARY_DIR}/qlinear-c-test
      )
    endif()
    joggle_script_test(mem-execution mem.cmake
      DEFINES
        CC=${JOGGLE_TEST_C_COMPILER}
        MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/plan.jog
        HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/mem_main.c
        ROOT=${CMAKE_CURRENT_BINARY_DIR}/mem-test
    )
    joggle_script_test(mem-safety mem_safety.cmake
      DEFINES
        CC=${JOGGLE_TEST_C_COMPILER}
        MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/mem_safety.jog
        HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/mem_safety_main.c
        ROOT=${CMAKE_CURRENT_BINARY_DIR}/mem-safety-test
    )
    if(JOGGLE_RESEARCH)
      add_test(
        NAME derive-memory
        COMMAND
          ${CMAKE_COMMAND}
          -DTOOL=$<TARGET_FILE:joggle-tool>
          -DCC=${JOGGLE_TEST_C_COMPILER}
          -DMODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/mem_safety.jog
          -DHARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/mem_safety_main.c
          -DMODULES=${CMAKE_CURRENT_BINARY_DIR}/modules
          -DROOT=${CMAKE_CURRENT_BINARY_DIR}/derive-memory-test
          -DEXPERIMENTS=${CMAKE_CURRENT_SOURCE_DIR}/paper/experiments
          -DTEST_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/test/mem_safety.cmake
          -P ${CMAKE_CURRENT_SOURCE_DIR}/paper/experiments/reuse/check.cmake
      )
    endif()
    joggle_script_test(dynamic-c-execution dynamic_c.cmake
      DEFINES
        CC=${JOGGLE_TEST_C_COMPILER}
        MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/dynamic_c.jog
        HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/dynamic_c_main.c
        ROOT=${CMAKE_CURRENT_BINARY_DIR}/dynamic-c-test
    )
    joggle_script_test(nms-c-execution nms_c.cmake
      DEFINES
        CC=${JOGGLE_TEST_C_COMPILER}
        MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/nms_c.jog
        HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/nms_c_main.c
        ROOT=${CMAKE_CURRENT_BINARY_DIR}/nms-c-test
    )
    joggle_script_test(nonzero-c-execution nonzero_c.cmake
      DEFINES
        CC=${JOGGLE_TEST_C_COMPILER}
        MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/nonzero_c.jog
        HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/nonzero_c_main.c
        ROOT=${CMAKE_CURRENT_BINARY_DIR}/nonzero-c-test
    )
    joggle_script_test(dynamic-slice-c-execution dynamic_slice_c.cmake
      DEFINES
        CC=${JOGGLE_TEST_C_COMPILER}
        MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/dynamic_slice_c.jog
        HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/dynamic_slice_c_main.c
        ROOT=${CMAKE_CURRENT_BINARY_DIR}/dynamic-slice-c-test
    )
    joggle_script_test(range-c-execution range_c.cmake
      DEFINES
        CC=${JOGGLE_TEST_C_COMPILER}
        MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/range_c.jog
        HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/range_c_main.c
        ROOT=${CMAKE_CURRENT_BINARY_DIR}/range-c-test
    )
    joggle_script_test(topk-c-execution topk_c.cmake
      DEFINES
        CC=${JOGGLE_TEST_C_COMPILER}
        MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/topk_c.jog
        HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/topk_c_main.c
        ROOT=${CMAKE_CURRENT_BINARY_DIR}/topk-c-test
    )
    joggle_script_test(resize-c-execution resize_c.cmake
      DEFINES
        CC=${JOGGLE_TEST_C_COMPILER}
        MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/resize_c.jog
        HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/resize_c_main.c
        ROOT=${CMAKE_CURRENT_BINARY_DIR}/resize-c-test
    )
    joggle_script_test(tile-execution tile.cmake
      DEFINES
        CC=${JOGGLE_TEST_C_COMPILER}
        MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile.jog
        FORM_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile_form.jog
        CUSTOM_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile_custom.jog
        HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile_main.c
        FUSE_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/mem.jog
        FUSE_HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/mem_main.c
        INVALID_FUSE_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile_invalid.jog
        EFFECT_FUSE_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile_effect.jog
        KEEP_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile_keep.jog
        KEEP_HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile_keep_main.c
        RELU_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile_relu.jog
        RELU_HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile_relu_main.c
        MULTI_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile_multi.jog
        MULTI_HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile_multi_main.c
        MERGE_MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile_merge.jog
        MERGE_HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/tile_merge_main.c
        EXTENSIONS=${CMAKE_CURRENT_SOURCE_DIR}/extensions
        ROOT=${CMAKE_CURRENT_BINARY_DIR}/tile-test
    )
  endif()
  joggle_script_test(cli-report report.cmake
    DEFINES
      MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/matmul.jog
      SOURCE_MODULES=${CMAKE_CURRENT_SOURCE_DIR}/modules
      BUILD_MODULES=${CMAKE_CURRENT_BINARY_DIR}/modules
      OUT=${CMAKE_CURRENT_BINARY_DIR}/run-report.attr
  )
  joggle_script_test(cli-module module.cmake
    DEFINES
      SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/modules
      BUILD_ROOT=${CMAKE_CURRENT_BINARY_DIR}/modules
      TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/module-test
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
        "-DGENERATOR=${CMAKE_GENERATOR}"
        "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
        "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
        "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DTOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
        "-DOSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}"
        "-DOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
        "-DSYSROOT=${CMAKE_SYSROOT}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/install.cmake
    )
  endif()
  if(JOGGLE_BUILD_ONNX AND NOT JOGGLE_TEST_ONNX_ZOO STREQUAL "")
    add_executable(joggle-onnx-test test/onnx.cpp)
    target_link_libraries(
      joggle-onnx-test PRIVATE Joggle::joggle joggle-onnx-proto
    )
    add_dependencies(joggle-onnx-test joggle-onnx)

    set(onnx_zoo_available)
    function(joggle_onnx_model)
      cmake_parse_arguments(
        MODEL "HEAVY"
        "NAME;SOURCE;URL;SHA256;TEST;APP_SOURCE;APP_TOP;APP_SHA256" "ARGS" ${ARGN}
      )
      foreach(field NAME SOURCE SHA256)
        if("${MODEL_${field}}" STREQUAL "")
          message(FATAL_ERROR "invalid ONNX model declaration: missing ${field}")
        endif()
      endforeach()
      if(MODEL_HEAVY AND NOT JOGGLE_TEST_ONNX_HEAVY)
        return()
      endif()
      set(model "${JOGGLE_TEST_ONNX_ZOO}/${MODEL_NAME}.onnx")
      if(NOT EXISTS "${model}")
        message(STATUS "Skipping absent ONNX Model Zoo case: ${MODEL_NAME}")
        return()
      endif()
      file(SHA256 "${model}" actual)
      if(NOT "${actual}" STREQUAL "${MODEL_SHA256}")
        message(FATAL_ERROR "ONNX Model Zoo hash mismatch: ${model}")
      endif()
      list(APPEND onnx_zoo_available "${MODEL_NAME}")
      set(onnx_zoo_available "${onnx_zoo_available}" PARENT_SCOPE)
      if(NOT "${MODEL_TEST}" STREQUAL "")
        add_test(
          NAME "${MODEL_TEST}"
          COMMAND joggle-onnx-zoo-test "${model}"
                  "${CMAKE_CURRENT_BINARY_DIR}/modules" ${MODEL_ARGS}
        )
        set(labels "onnx-zoo;onnx-zoo-record")
        if(MODEL_HEAVY)
          list(APPEND labels heavy)
          set_tests_properties("${MODEL_TEST}" PROPERTIES TIMEOUT 900)
        endif()
        set_tests_properties("${MODEL_TEST}" PROPERTIES LABELS "${labels}")
      endif()
    endfunction()

    include("${CMAKE_CURRENT_SOURCE_DIR}/test/models.cmake")
    if(mobilenetv2-7 IN_LIST onnx_zoo_available)
      add_test(
        NAME onnx-zoo-mobilenet-contract
        COMMAND joggle-onnx-test
                ${JOGGLE_TEST_ONNX_ZOO}/mobilenetv2-7.onnx
                ${CMAKE_CURRENT_BINARY_DIR}/modules
      )
      set_tests_properties(
        onnx-zoo-mobilenet-contract PROPERTIES LABELS "onnx-zoo"
      )
    endif()
  endif()
  if(JOGGLE_BUILD_ONNX)
    add_executable(joggle-onnx-zoo-test test/zoo.cpp)
    target_link_libraries(joggle-onnx-zoo-test PRIVATE Joggle::joggle)
    add_dependencies(joggle-onnx-zoo-test joggle-onnx)
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
  if(JOGGLE_BUILD_ONNX AND JOGGLE_TEST_C_COMPILER)
    add_executable(joggle-onnx-execute-test test/onnx_execute.cpp)
    target_link_libraries(
      joggle-onnx-execute-test PRIVATE Joggle::joggle joggle-onnx-proto
    )
    add_dependencies(joggle-onnx-execute-test joggle-onnx joggle-vm)

    set(onnx_execute_files
      model.onnx
      test_data_set_0/input_0.pb
      test_data_set_0/input_1.pb
      test_data_set_0/output_0.pb
    )

    function(joggle_check_onnx_case case)
      set(hashes ${ARGN})
      list(LENGTH hashes hash_count)
      if(NOT hash_count EQUAL 4)
        message(FATAL_ERROR "ONNX case requires four hashes: ${case}")
      endif()
      foreach(index RANGE 3)
        list(GET onnx_execute_files ${index} file)
        list(GET hashes ${index} expected)
        if(NOT EXISTS "${case}/${file}")
          message(FATAL_ERROR "missing pinned ONNX file: ${case}/${file}")
        endif()
        file(SHA256 "${case}/${file}" actual)
        if(NOT actual STREQUAL expected)
          message(FATAL_ERROR "ONNX hash mismatch: ${case}/${file}")
        endif()
      endforeach()
    endfunction()

    function(joggle_onnx_execution name case labels)
      add_test(
        NAME ${name}
        COMMAND
          ${CMAKE_COMMAND}
          -DTEST=$<TARGET_FILE:joggle-onnx-execute-test>
          -DCC=${JOGGLE_TEST_C_COMPILER}
          -DCASE=${case}
          -DMODULES=${CMAKE_CURRENT_BINARY_DIR}/modules
          -DEXTENSIONS=${CMAKE_CURRENT_SOURCE_DIR}/extensions
          -DROOT=${CMAKE_CURRENT_BINARY_DIR}/${name}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/test/onnx_execute.cmake
      )
      set_tests_properties(${name} PROPERTIES LABELS "${labels}")
      if(APPLE AND CMAKE_CXX_FLAGS MATCHES "sanitize=address")
        # Homebrew protobuf-lite is not ASan-instrumented, while Apple's libc++
        # annotates std::string capacity in instrumented callers. Disable only
        # those container annotations for this mixed-ABI fixture parser; normal
        # address and undefined-behavior checks remain active.
        set_property(
          TEST ${name} PROPERTY ENVIRONMENT
          "ASAN_OPTIONS=detect_container_overflow=0"
        )
      endif()
    endfunction()

    # The matched-implementation contract and its fixture belong to the paper
    # workspace, so this check is gated with the rest of the research checks.
    if(JOGGLE_RESEARCH)
      set(onnx_matched_case
          "${CMAKE_CURRENT_SOURCE_DIR}/paper/fixtures/implementation")
      joggle_check_onnx_case(
        "${onnx_matched_case}"
        662fd988387d6e33bce053676711295f8b6f82b83baf171412d4a48f0e6c7284
        b819079e4e6f60a2b29c5473b2d0e77a56bfc0fc3766ee48d3ef173e7b4854e6
        0af081f30a3c7f3b58048f7f1642393afbc5a431bae498c4effe6c820c698b03
        ca2186ef8510389441295b5dc2f952e15f8c645ad44c05c56e7ea69428bd8195
      )
      joggle_onnx_execution(
        onnx-matched-implementation "${onnx_matched_case}"
        "onnx;execution;paper"
      )
    endif()

    if(NOT JOGGLE_TEST_ONNX_BACKEND STREQUAL "")
      set(onnx_execute_case "${JOGGLE_TEST_ONNX_BACKEND}/test_matmul_2d")
      joggle_check_onnx_case(
        "${onnx_execute_case}"
        946f3a49ccfdaf62586bbae8438065fe41a7e6a9b8006b14af3219999b49bb76
        5c9fa13b29d31b4105ea162c0caecc951804499b016bdf268b8f470b404c1f79
        7d950d6d6af29dfa3acab8c9fcdea76d553c11124013b106eb6622bc0ebf3dd4
        c7928ba3e8fe85076974c955200532c2a24697119eb54b338fbc7c2639228e48
      )
      joggle_onnx_execution(
        onnx-execution "${onnx_execute_case}" "onnx;execution"
      )
    endif()
  endif()
  if(JOGGLE_BUILD_ONNX AND JOGGLE_TEST_C_COMPILER AND
     (NOT JOGGLE_EXAMPLE_MNIST STREQUAL "" OR
      NOT JOGGLE_EXAMPLE_MOBILENET STREQUAL ""))
    find_program(JOGGLE_PYTHON_EXECUTABLE NAMES python3 python REQUIRED)
    add_executable(joggle-onnx-app test/onnx-app/app.cpp)
    target_link_libraries(
      joggle-onnx-app PRIVATE Joggle::joggle joggle-onnx-proto
    )
    add_dependencies(joggle-onnx-app joggle-onnx joggle-vm)

    function(joggle_onnx_app name case model model_hash input_hash output_hash
             vm timeout labels)
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
          -DPYTHON=${JOGGLE_PYTHON_EXECUTABLE}
          -DGENERATOR=${CMAKE_CURRENT_SOURCE_DIR}/test/tools/make_harness.py
          -DROOT=${CMAKE_CURRENT_BINARY_DIR}/onnx-app/${name}
          -DVM=${vm}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/test/onnx-app/run.cmake
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
      ON 120 "onnx;execution"
    )
    joggle_onnx_app(
      mobilenet "${JOGGLE_EXAMPLE_MOBILENET}" mobilenetv2-7.onnx
      c1c513582d56afceff8516c73804e484c81c6a830712ab6d682253f4a3cd042f
      8d2dd7bc0a81404c443315a7cdcec81b502082cf3f450cec204b7278be85a254
      55c3689e6946111fd78ad250e857c36d58fd383819dd0d3997cd1332c7d86cbd
      OFF 1200 "heavy;onnx;execution"
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
      joggle_script_test(sat-c-execution sat_c.cmake
        DEFINES
          CC=${JOGGLE_TEST_C_COMPILER}
          MODEL=${CMAKE_CURRENT_SOURCE_DIR}/test/data/sat_c.jog
          HARNESS=${CMAKE_CURRENT_SOURCE_DIR}/test/data/sat_c_main.c
          ROOT=${CMAKE_CURRENT_BINARY_DIR}/sat-c
      )
    endif()
  endif()

  # Every test carries exactly one kind label, so a useful subset is selectable
  # without knowing test names:
  #
  #   ctest -LE model      the everyday loop: everything except pinned models
  #   ctest -L unit        native C++ tests
  #   ctest -L c           generated-C emission, compilation, and execution
  #   ctest -L extension   out-of-tree module packages under extensions/
  #   ctest -L example     example applications; the suite does not need them
  #   ctest -L model       pinned ONNX and TFLite gates; minutes, not seconds
  #
  # Existing labels such as onnx-zoo are preserved, because docs/guide/
  # documents `ctest -L onnx-zoo`.
  joggle_label(unit
    workflow network lang-robustness edit-robustness module-robustness vm
    onnx-codec tflite tflite-value sat
  )
  joggle_label(cli
    cli-version cli-check cli-run cli-query cli-args cli-stream
    cli-diagnostics cli-emit cli-report cli-module
  )
  joggle_label(c
    c-execution quant-c-execution qlinear-c-execution mem-execution
    mem-safety dynamic-c-execution dynamic-slice-c-execution nms-c-execution
    nonzero-c-execution range-c-execution topk-c-execution resize-c-execution
    tile-execution sat-c-execution
  )
  joggle_label(analysis stat-summary bounds-report opt-overloads)
  joggle_label(extension
    ikj-extension edge-extension locality-extension compact-extension
    cost-extension cost-configured-extension
  )
  joggle_label(example onnx-app-mnist onnx-app-mobilenet)
  joggle_label(model
    onnx-zoo-mnist onnx-zoo-mobilenet onnx-zoo-squeezenet
    onnx-zoo-squeezenet-qdq onnx-zoo-resnet onnx-zoo-tinyyolo
    onnx-zoo-tinyyolov3 onnx-zoo-ultraface onnx-zoo-ssd-mobilenet
    onnx-zoo-shufflenet onnx-zoo-densenet onnx-zoo-googlenet
    onnx-zoo-mobilenet-contract
  )
  joggle_label(install install-consumer)
  joggle_label(research
    paper-fixtures onnx-mlir-policy-record onnx-mlir-numeric-format-record
    measure-systems derive-memory onnx-matched-implementation
  )
endif()
