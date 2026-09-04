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
  src/exec_val.cpp
  src/expr_syntax.cpp
  src/ir.cpp
  src/mod_ir.cpp
  src/fn_body.cpp
  src/mod.cpp
  src/repository.cpp
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

include(cmake/JoggleMod.cmake)

joggle_mod(joggle_transform_native
  SOURCE mods/transform/mod.joggle
  NATIVE mods/transform/native.cpp)
if(MSVC)
  target_compile_options(joggle_transform_native PRIVATE /W4 /WX)
else()
  target_compile_options(joggle_transform_native PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
set_target_properties(joggle_transform_native PROPERTIES
  PREFIX ""
  OUTPUT_NAME "native"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/mods/transform"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/mods/transform")

joggle_mod(joggle_quant_native
  SOURCE mods/quant/mod.joggle
  NATIVE mods/quant/native.cpp)
if(MSVC)
  target_compile_options(joggle_quant_native PRIVATE /W4 /WX)
else()
  target_compile_options(joggle_quant_native PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
set_target_properties(joggle_quant_native PROPERTIES
  PREFIX ""
  OUTPUT_NAME "native"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/mods/quant"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/mods/quant")

set(JOGGLE_SQUEEZENET_MODEL
  "${CMAKE_CURRENT_BINARY_DIR}/models/squeezenet1.1-7.onnx"
  CACHE FILEPATH "Build-cache path for the audited SqueezeNet model")
set(JOGGLE_SQUEEZENET_QDQ_MODEL
  "${CMAKE_CURRENT_BINARY_DIR}/models/squeezenet1.0-13-qdq.onnx"
  CACHE FILEPATH "Build-cache path for the audited QDQ SqueezeNet model")
set(JOGGLE_ONNX_TEST_MODEL "" CACHE FILEPATH
  "Optional local path to the audited SqueezeNet model for ONNX tests")
set(JOGGLE_ONNX_QDQ_TEST_MODEL "" CACHE FILEPATH
  "Optional local path to the audited QDQ SqueezeNet model for ONNX tests")
set(JOGGLE_MODEL_ZOO_ROOT
  "https://media.githubusercontent.com/media/onnx/models/4c46cd00fbdb7cd30b6c1c17ab54f2e1f4f7b177/validated/vision/classification/squeezenet/model")
add_custom_target(joggle_fetch_squeezenet
  COMMAND ${CMAKE_COMMAND}
    "-DJOGGLE_MODEL_OUTPUT=${JOGGLE_SQUEEZENET_MODEL}"
    "-DJOGGLE_MODEL_URL=${JOGGLE_MODEL_ZOO_ROOT}/squeezenet1.1-7.onnx"
    "-DJOGGLE_MODEL_SHA256=1eeff551a67ae8d565ca33b572fc4b66e3ef357b0eb2863bb9ff47a918cc4088"
    "-DJOGGLE_MODEL_NAME=SqueezeNet 1.1 opset 7"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchModel.cmake"
  COMMENT "Fetch the hash-pinned ONNX Model Zoo SqueezeNet model"
  VERBATIM)
add_custom_target(joggle_fetch_squeezenet_qdq
  COMMAND ${CMAKE_COMMAND}
    "-DJOGGLE_MODEL_OUTPUT=${JOGGLE_SQUEEZENET_QDQ_MODEL}"
    "-DJOGGLE_MODEL_URL=${JOGGLE_MODEL_ZOO_ROOT}/squeezenet1.0-13-qdq.onnx"
    "-DJOGGLE_MODEL_SHA256=4a567dd7542ef440890d57268fabf47211174c593d7a1837bd7f16a1067169e7"
    "-DJOGGLE_MODEL_NAME=SqueezeNet 1.0 QDQ opset 13"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchModel.cmake"
  COMMENT "Fetch the hash-pinned ONNX Model Zoo QDQ SqueezeNet model"
  VERBATIM)

if(JOGGLE_BUILD_ONNX)
  find_package(Protobuf REQUIRED)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(JOGGLE_PROTOBUF QUIET IMPORTED_TARGET protobuf)
  endif()
  set(JOGGLE_ONNX_PROTO
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/onnx/third_party/onnx.proto")
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
  joggle_mod(joggle_onnx_native
    SOURCE mods/onnx/mod.joggle
    NATIVE
      mods/onnx/native.cpp
      mods/onnx/import.cpp)
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
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/mods/onnx"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/mods/onnx")
  install(TARGETS joggle_onnx_native
    LIBRARY DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/mods/onnx
    RUNTIME DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/mods/onnx)
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
install(FILES mods/tensor/mod.joggle
  DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/mods/tensor)
install(FILES mods/transform/mod.joggle
  DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/mods/transform)
install(FILES mods/quant/mod.joggle
  DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/mods/quant)
install(FILES mods/onnx/mod.joggle
  DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/mods/onnx)
install(TARGETS joggle_transform_native
  LIBRARY DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/mods/transform
  RUNTIME DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/mods/transform)
install(TARGETS joggle_quant_native
  LIBRARY DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/mods/quant
  RUNTIME DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/mods/quant)
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
  cmake/JoggleMod.cmake
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
  set(JOGGLE_TEST_MOD
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/compiler_test.joggle")
  set(JOGGLE_TENSOR_MOD
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/tensor/mod.joggle")
  set(JOGGLE_TRANSFORM_MOD
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/transform/mod.joggle")
  set(JOGGLE_QUANT_MOD
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/quant/mod.joggle")

  joggle_mod(joggle_compiler_test_native
    SOURCE tests/fixtures/compiler_test.joggle
    NATIVE tests/compiler_test_native.cpp)
  set_target_properties(joggle_compiler_test_native PROPERTIES
    PREFIX ""
    OUTPUT_NAME "compiler_test_native"
  )

  add_executable(joggle_mod_test tests/mod_test.cpp)
  target_link_libraries(joggle_mod_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_mod_test PRIVATE
    JOGGLE_TEST_MOD="${JOGGLE_TEST_MOD}"
  )
  add_test(NAME mod COMMAND joggle_mod_test)

  add_executable(joggle_type_test tests/type_test.cpp)
  target_link_libraries(joggle_type_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_type_test PRIVATE
    JOGGLE_TEST_MOD="${JOGGLE_TEST_MOD}"
  )
  add_test(NAME type COMMAND joggle_type_test)

  add_executable(joggle_tensor_test tests/tensor_test.cpp)
  target_link_libraries(joggle_tensor_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_tensor_test PRIVATE
    JOGGLE_TENSOR_MOD="${JOGGLE_TENSOR_MOD}"
  )
  add_test(NAME tensor COMMAND joggle_tensor_test)

  add_executable(joggle_quant_test tests/quant_test.cpp)
  target_link_libraries(joggle_quant_test PRIVATE joggle::joggle)
  add_dependencies(joggle_quant_test joggle_quant_native)
  target_compile_definitions(joggle_quant_test PRIVATE
    JOGGLE_QUANT_MOD="${JOGGLE_QUANT_MOD}"
    JOGGLE_QUANT_NATIVE="$<TARGET_FILE:joggle_quant_native>"
  )
  add_test(NAME quant COMMAND joggle_quant_test)

  add_executable(joggle_ir_test tests/ir_test.cpp)
  target_link_libraries(joggle_ir_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_ir_test PRIVATE
    JOGGLE_TEST_MOD="${JOGGLE_TEST_MOD}"
  )
  add_test(NAME ir COMMAND joggle_ir_test)

  add_executable(joggle_mod_ir_test tests/mod_ir_test.cpp)
  target_link_libraries(joggle_mod_ir_test PRIVATE joggle::joggle)
  add_test(NAME mod_ir COMMAND joggle_mod_ir_test)

  add_executable(joggle_transform_test tests/transform_test.cpp)
  target_link_libraries(joggle_transform_test PRIVATE joggle::joggle)
  target_include_directories(joggle_transform_test PRIVATE src)
  add_test(NAME transform COMMAND joggle_transform_test)

  add_executable(joggle_transform_mod_test
    tests/transform_mod_test.cpp)
  target_link_libraries(joggle_transform_mod_test PRIVATE joggle::joggle)
  add_dependencies(joggle_transform_mod_test joggle_transform_native)
  target_compile_definitions(joggle_transform_mod_test PRIVATE
    JOGGLE_TRANSFORM_MOD="${JOGGLE_TRANSFORM_MOD}"
    JOGGLE_TRANSFORM_NATIVE="$<TARGET_FILE:joggle_transform_native>"
  )
  add_test(NAME transform_mod COMMAND joggle_transform_mod_test)

  add_executable(joggle_equivalence_test tests/equivalence_test.cpp)
  target_link_libraries(joggle_equivalence_test PRIVATE joggle::joggle)
  add_test(NAME equivalence COMMAND joggle_equivalence_test)

  add_executable(joggle_specialization_test tests/specialization_test.cpp)
  target_link_libraries(joggle_specialization_test PRIVATE joggle::joggle)
  add_test(NAME specialization COMMAND joggle_specialization_test)

  add_executable(joggle_fn_body_test tests/fn_body_test.cpp)
  target_link_libraries(joggle_fn_body_test PRIVATE joggle::joggle)
  add_test(NAME fn_body COMMAND joggle_fn_body_test)

  add_test(NAME fn_cli
    COMMAND $<TARGET_FILE:joggle_cli> check
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fn_cli.joggle
      --with ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native_plugin.joggle
  )
  add_test(NAME fn_run_cli
    COMMAND ${CMAKE_COMMAND}
      -DJOGGLE_CLI=$<TARGET_FILE:joggle_cli>
      -DJOGGLE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fn_cli.joggle
      -DJOGGLE_DEPENDENCY=${CMAKE_CURRENT_BINARY_DIR}/native_plugin.joggle
      -DJOGGLE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/fn_run_output.bin
      -DJOGGLE_NATIVE=$<TARGET_FILE:joggle_test_native>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/fn_run_cli.cmake
  )

  add_executable(joggle_binding_test tests/binding_test.cpp)
  target_link_libraries(joggle_binding_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_binding_test PRIVATE
    JOGGLE_TEST_MOD="${JOGGLE_TEST_MOD}"
  )
  add_test(NAME binding COMMAND joggle_binding_test)

  configure_file(tests/fixtures/native_plugin.joggle
    ${CMAKE_CURRENT_BINARY_DIR}/native_plugin.joggle COPYONLY)
  joggle_mod(joggle_test_native
    SOURCE tests/fixtures/native_plugin.joggle
    NATIVE tests/native_plugin.cpp)
  set_target_properties(joggle_test_native PROPERTIES
    PREFIX ""
    OUTPUT_NAME "native"
  )
  joggle_mod(joggle_test_native_failure
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
    JOGGLE_PRELUDE_MOD="${CMAKE_CURRENT_SOURCE_DIR}/language/prelude.joggle")
  add_test(NAME prelude COMMAND joggle_prelude_test)

  add_executable(joggle_compiler_fn_test tests/compiler_fn_test.cpp)
  target_link_libraries(joggle_compiler_fn_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_compiler_fn_test PRIVATE
    JOGGLE_TEST_MOD="${JOGGLE_TEST_MOD}"
    JOGGLE_TEST_NATIVE="$<TARGET_FILE:joggle_compiler_test_native>"
  )
  add_test(NAME compiler_fn COMMAND joggle_compiler_fn_test)

  add_executable(joggle_sha256_test tests/sha256_test.cpp)
  target_link_libraries(joggle_sha256_test PRIVATE joggle::joggle)
  add_test(NAME sha256 COMMAND joggle_sha256_test)

  add_executable(joggle_repository_test tests/repository_test.cpp)
  target_link_libraries(joggle_repository_test PRIVATE joggle::joggle)
  target_include_directories(joggle_repository_test PRIVATE src)
  add_test(NAME repository COMMAND joggle_repository_test)

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

  add_test(NAME repository_cli
    COMMAND ${CMAKE_COMMAND}
      -DJOGGLE_CLI=$<TARGET_FILE:joggle_cli>
      -DJOGGLE_REPOSITORY_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-repository
      -DJOGGLE_LOCK_CONSUMER=$<TARGET_FILE:joggle_lock_consumer>
      -DJOGGLE_NATIVE_MOD=${CMAKE_CURRENT_BINARY_DIR}/native_plugin.joggle
      -DJOGGLE_NATIVE_LIBRARY=$<TARGET_FILE:joggle_test_native>
      -DJOGGLE_NATIVE_FAILURE=$<TARGET_FILE:joggle_test_native_failure>
      -DJOGGLE_NATIVE_LOADER=$<TARGET_FILE:joggle_native_loader_test>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/repository_cli.cmake
  )

  if(JOGGLE_BUILD_ONNX)
    add_executable(joggle_onnx_test tests/onnx_test.cpp)
    target_link_libraries(joggle_onnx_test PRIVATE joggle::joggle)
    add_test(NAME onnx
      COMMAND joggle_onnx_test
        ${JOGGLE_TENSOR_MOD}
        ${CMAKE_CURRENT_SOURCE_DIR}/mods/onnx/mod.joggle
        $<TARGET_FILE:joggle_onnx_native>
        ${JOGGLE_ONNX_TEST_MODEL})

    if(NOT JOGGLE_ONNX_QDQ_TEST_MODEL STREQUAL "")
      add_executable(joggle_onnx_qdq_test tests/onnx_qdq_test.cpp)
      target_link_libraries(joggle_onnx_qdq_test PRIVATE joggle::joggle)
      add_test(NAME onnx_qdq
        COMMAND joggle_onnx_qdq_test
          ${JOGGLE_TENSOR_MOD}
          ${JOGGLE_QUANT_MOD}
          ${CMAKE_CURRENT_SOURCE_DIR}/mods/onnx/mod.joggle
          $<TARGET_FILE:joggle_onnx_native>
          ${JOGGLE_ONNX_QDQ_TEST_MODEL})

    endif()

    if(NOT JOGGLE_ONNX_TEST_MODEL STREQUAL "")
      add_test(NAME onnx_bundle_cli
        COMMAND ${CMAKE_COMMAND}
          -DJOGGLE_CLI=$<TARGET_FILE:joggle_cli>
          -DJOGGLE_TENSOR_MOD=${JOGGLE_TENSOR_MOD}
          -DJOGGLE_ONNX_MOD=${CMAKE_CURRENT_SOURCE_DIR}/mods/onnx/mod.joggle
          -DJOGGLE_ONNX_NATIVE=$<TARGET_FILE:joggle_onnx_native>
          -DJOGGLE_ONNX_MODEL=${JOGGLE_ONNX_TEST_MODEL}
          -DJOGGLE_EXPECTED_PAYLOADS=54
          -DJOGGLE_BUNDLE_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/onnx-bundle-test
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/onnx_bundle_cli.cmake)
    endif()

    if(NOT JOGGLE_ONNX_QDQ_TEST_MODEL STREQUAL "")
      add_test(NAME onnx_qdq_bundle_cli
        COMMAND ${CMAKE_COMMAND}
          -DJOGGLE_CLI=$<TARGET_FILE:joggle_cli>
          -DJOGGLE_TENSOR_MOD=${JOGGLE_TENSOR_MOD}
          -DJOGGLE_QUANT_MOD=${JOGGLE_QUANT_MOD}
          -DJOGGLE_ONNX_MOD=${CMAKE_CURRENT_SOURCE_DIR}/mods/onnx/mod.joggle
          -DJOGGLE_ONNX_NATIVE=$<TARGET_FILE:joggle_onnx_native>
          -DJOGGLE_ONNX_MODEL=${JOGGLE_ONNX_QDQ_TEST_MODEL}
          -DJOGGLE_MODEL_NAME=squeezenet_qdq
          -DJOGGLE_EXPECTED_PAYLOADS=148
          -DJOGGLE_BUNDLE_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/onnx-qdq-bundle-test
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
      add_executable(joggle_quant_runtime_model
        tests/quant_runtime_model.cpp)
      target_link_libraries(joggle_quant_runtime_model PRIVATE
        joggle::joggle joggle_onnx_proto)
      add_dependencies(joggle_quant_runtime_model joggle_quant_native)
      if(MSVC)
        target_compile_options(joggle_quant_runtime_model PRIVATE /W4 /WX)
      else()
        target_compile_options(joggle_quant_runtime_model PRIVATE
          -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
      endif()
      add_test(NAME quant_runtime
        COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/tests/quant_runtime_test.py
          --generator $<TARGET_FILE:joggle_quant_runtime_model>
          --quant ${JOGGLE_QUANT_MOD}
          --native $<TARGET_FILE:joggle_quant_native>
          --model ${CMAKE_CURRENT_BINARY_DIR}/quant-reference.onnx
          --quantized ${CMAKE_CURRENT_BINARY_DIR}/quant-reference-i8.bin
          --dequantized ${CMAKE_CURRENT_BINARY_DIR}/quant-reference-f32.bin)
      add_test(NAME onnx_runtime
        COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/tests/onnx_runtime_test.py
          --emitter $<TARGET_FILE:joggle_onnx_roundtrip>
          --tensor ${JOGGLE_TENSOR_MOD}
          --onnx ${CMAKE_CURRENT_SOURCE_DIR}/mods/onnx/mod.joggle
          --native $<TARGET_FILE:joggle_onnx_native>
          --model ${JOGGLE_ONNX_TEST_MODEL}
          --output ${CMAKE_CURRENT_BINARY_DIR}/squeezenet-roundtrip.onnx)
      if(NOT JOGGLE_ONNX_QDQ_TEST_MODEL STREQUAL "")
        add_test(NAME onnx_qdq_runtime
          COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/onnx_runtime_test.py
            --emitter $<TARGET_FILE:joggle_onnx_roundtrip>
            --tensor ${JOGGLE_TENSOR_MOD}
            --quant ${JOGGLE_QUANT_MOD}
            --onnx ${CMAKE_CURRENT_SOURCE_DIR}/mods/onnx/mod.joggle
            --native $<TARGET_FILE:joggle_onnx_native>
            --model ${JOGGLE_ONNX_QDQ_TEST_MODEL}
            --output ${CMAKE_CURRENT_BINARY_DIR}/squeezenet-qdq-roundtrip.onnx)
      endif()
    endif()
  endif()
endif()
