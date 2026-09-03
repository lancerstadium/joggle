cmake_minimum_required(VERSION 3.20)

project(Joggle VERSION 0.1.0 LANGUAGES CXX)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(JOGGLE_BUILD_TESTS "Build Joggle tests" ON)

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
  src/execution.cpp
  src/execution_value.cpp
  src/expression_syntax.cpp
  src/ir.cpp
  src/module_ir.cpp
  src/function_body.cpp
  src/module.cpp
  src/module_repository.cpp
  src/sha256.cpp
  src/syntax_lexer.cpp
  src/type.cpp
  src/type_contract.cpp
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

include(cmake/JoggleBehavior.cmake)

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
install(DIRECTORY modules/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules
  FILES_MATCHING PATTERN "*.joggle" PATTERN "README.md")
install(FILES language/prelude.joggle
  DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/language)
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
  cmake/JoggleBehavior.cmake
  cmake/GenerateBehaviorIdentity.cmake
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

  joggle_add_behavior(joggle_compiler_test_behavior
    MODULE tests/fixtures/compiler_test.joggle
    SOURCES tests/compiler_test_behavior.cpp)
  set_target_properties(joggle_compiler_test_behavior PROPERTIES
    PREFIX ""
    OUTPUT_NAME "compiler_test_behavior"
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
  add_test(NAME transform COMMAND joggle_transform_test)

  add_executable(joggle_function_body_test tests/function_body_test.cpp)
  target_link_libraries(joggle_function_body_test PRIVATE joggle::joggle)
  add_test(NAME function_body COMMAND joggle_function_body_test)

  add_executable(joggle_nn_test tests/nn_test.cpp)
  target_link_libraries(joggle_nn_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_nn_test PRIVATE
    JOGGLE_ARITH_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/modules/arith.joggle"
    JOGGLE_TENSOR_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/modules/tensor.joggle"
    JOGGLE_BUFFER_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/modules/buffer.joggle"
    JOGGLE_NN_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/modules/nn.joggle"
    JOGGLE_RESNET_BLOCK="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/resnet18_basic_block.joggle"
  )
  add_test(NAME nn COMMAND joggle_nn_test)
  add_test(NAME nn_cli
    COMMAND $<TARGET_FILE:joggle_cli> check
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/resnet18_basic_block.joggle
      --with ${CMAKE_CURRENT_SOURCE_DIR}/modules/nn.joggle
      --with ${CMAKE_CURRENT_SOURCE_DIR}/modules/tensor.joggle
      --with ${CMAKE_CURRENT_SOURCE_DIR}/modules/arith.joggle
  )

  joggle_add_behavior(joggle_nn_pipeline_behavior
    MODULE examples/nn_pipeline/nn_pipeline.joggle
    SOURCES examples/nn_pipeline/behavior.cpp)
  set_target_properties(joggle_nn_pipeline_behavior PROPERTIES
    PREFIX ""
    OUTPUT_NAME "nn_pipeline"
  )
  add_executable(joggle_nn_pipeline_test tests/nn_pipeline_test.cpp)
  target_link_libraries(joggle_nn_pipeline_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_nn_pipeline_test PRIVATE
    JOGGLE_ARITH_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/modules/arith.joggle"
    JOGGLE_TENSOR_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/modules/tensor.joggle"
    JOGGLE_NN_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/modules/nn.joggle"
    JOGGLE_RESNET_BLOCK="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/resnet18_basic_block.joggle"
    JOGGLE_EXAMPLE_ACCEL_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/examples/nn_pipeline/example_accel.joggle"
    JOGGLE_NN_PIPELINE_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/examples/nn_pipeline/nn_pipeline.joggle"
    JOGGLE_NN_PIPELINE_BEHAVIOR="$<TARGET_FILE:joggle_nn_pipeline_behavior>"
  )
  add_test(NAME nn_pipeline COMMAND joggle_nn_pipeline_test)

  add_test(NAME function_cli
    COMMAND $<TARGET_FILE:joggle_cli> check
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/function_cli.joggle
  )
  add_test(NAME function_run_cli
    COMMAND ${CMAKE_COMMAND}
      -DJOGGLE_CLI=$<TARGET_FILE:joggle_cli>
      -DJOGGLE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/function_cli.joggle
      -DJOGGLE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/function_run_output.joggle
      -DJOGGLE_BEHAVIOR_SOURCE=${CMAKE_CURRENT_BINARY_DIR}/behavior_plugin.joggle
      -DJOGGLE_BEHAVIOR=$<TARGET_FILE:joggle_test_behavior>
      -DJOGGLE_TARGET=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/function_target.joggle
      -DJOGGLE_MODULE_TRANSFORM=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/module_transform.joggle
      -DJOGGLE_MODULE_TRANSFORM_BEHAVIOR=$<TARGET_FILE:joggle_module_transform_behavior>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/function_run_cli.cmake
  )

  add_executable(joggle_behavior_test tests/behavior_test.cpp)
  target_link_libraries(joggle_behavior_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_behavior_test PRIVATE
    JOGGLE_TEST_MODULE="${JOGGLE_TEST_MODULE}"
  )
  add_test(NAME behavior COMMAND joggle_behavior_test)

  configure_file(tests/fixtures/behavior_plugin.joggle
    ${CMAKE_CURRENT_BINARY_DIR}/behavior_plugin.joggle COPYONLY)
  joggle_add_behavior(joggle_test_behavior
    MODULE tests/fixtures/behavior_plugin.joggle
    SOURCES tests/behavior_plugin.cpp)
  set_target_properties(joggle_test_behavior PROPERTIES
    PREFIX ""
    OUTPUT_NAME "behavior"
  )
  joggle_add_behavior(joggle_test_behavior_failure
    MODULE tests/fixtures/behavior_plugin.joggle
    SOURCES tests/behavior_plugin.cpp)
  target_compile_definitions(joggle_test_behavior_failure PRIVATE
    JOGGLE_TEST_BEHAVIOR_FAIL=1)
  set_target_properties(joggle_test_behavior_failure PROPERTIES
    PREFIX ""
    OUTPUT_NAME "behavior_failure"
  )
  joggle_add_behavior(joggle_module_transform_behavior
    MODULE tests/fixtures/module_transform.joggle
    SOURCES tests/module_transform_behavior.cpp)
  set_target_properties(joggle_module_transform_behavior PROPERTIES
    PREFIX ""
    OUTPUT_NAME "module_transform"
  )
  add_executable(joggle_behavior_loader_test tests/behavior_loader_test.cpp)
  target_link_libraries(joggle_behavior_loader_test PRIVATE joggle::joggle)
  add_test(NAME behavior_loader COMMAND joggle_behavior_loader_test
    ${CMAKE_CURRENT_BINARY_DIR}/behavior_plugin.joggle
    $<TARGET_FILE:joggle_test_behavior>
    $<TARGET_FILE:joggle_test_behavior_failure>
  )

  add_executable(joggle_interface_test tests/interface_test.cpp)
  target_link_libraries(joggle_interface_test PRIVATE joggle::joggle)
  add_test(NAME interface COMMAND joggle_interface_test)

  add_executable(joggle_prelude_test tests/prelude_test.cpp)
  target_link_libraries(joggle_prelude_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_prelude_test PRIVATE
    JOGGLE_PRELUDE_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/language/prelude.joggle")
  add_test(NAME prelude COMMAND joggle_prelude_test)

  add_executable(joggle_compiler_function_test tests/compiler_function_test.cpp)
  target_link_libraries(joggle_compiler_function_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_compiler_function_test PRIVATE
    JOGGLE_TEST_MODULE="${JOGGLE_TEST_MODULE}"
    JOGGLE_TEST_BEHAVIOR="$<TARGET_FILE:joggle_compiler_test_behavior>"
  )
  add_test(NAME compiler_function COMMAND joggle_compiler_function_test)

  add_executable(joggle_sha256_test tests/sha256_test.cpp src/sha256.cpp)
  target_compile_features(joggle_sha256_test PRIVATE cxx_std_20)
  target_include_directories(joggle_sha256_test PRIVATE src)
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

  add_test(NAME package_cli
    COMMAND ${CMAKE_COMMAND}
      -DJOGGLE_CLI=$<TARGET_FILE:joggle_cli>
      -DJOGGLE_PACKAGE_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-packages
      -DJOGGLE_LOCK_CONSUMER=$<TARGET_FILE:joggle_lock_consumer>
      -DJOGGLE_BEHAVIOR_MODULE=${CMAKE_CURRENT_BINARY_DIR}/behavior_plugin.joggle
      -DJOGGLE_BEHAVIOR_LIBRARY=$<TARGET_FILE:joggle_test_behavior>
      -DJOGGLE_BEHAVIOR_FAILURE=$<TARGET_FILE:joggle_test_behavior_failure>
      -DJOGGLE_BEHAVIOR_LOADER=$<TARGET_FILE:joggle_behavior_loader_test>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_cli.cmake
  )
endif()
