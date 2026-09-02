cmake_minimum_required(VERSION 3.20)

project(Joggle VERSION 0.1.0 LANGUAGES CXX)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(JOGGLE_BUILD_TESTS "Build Joggle tests" ON)
option(JOGGLE_BUILD_EXAMPLES "Build example behavior libraries"
  ${JOGGLE_BUILD_TESTS})

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(joggle
  src/compiler.cpp
  src/diagnostic.cpp
  src/graph.cpp
  src/graph_member.cpp
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

if(JOGGLE_BUILD_EXAMPLES OR JOGGLE_BUILD_TESTS)
  joggle_add_behavior(joggle_arith_behavior
    MODULE examples/arith.joggle
    SOURCES examples/arith_behavior.cpp)
  set_target_properties(joggle_arith_behavior PROPERTIES
    PREFIX ""
    OUTPUT_NAME "arith_behavior"
  )
  joggle_add_behavior(joggle_tensor_behavior
    MODULE examples/tensor.joggle
    SOURCES examples/tensor_behavior.cpp)
  set_target_properties(joggle_tensor_behavior PROPERTIES
    PREFIX ""
    OUTPUT_NAME "tensor_behavior"
  )
  joggle_add_behavior(joggle_edgevec_behavior
    MODULE examples/edgevec.joggle
    SOURCES examples/edgevec_behavior.cpp)
  set_target_properties(joggle_edgevec_behavior PROPERTIES
    PREFIX ""
    OUTPUT_NAME "edgevec_behavior"
  )
  joggle_add_behavior(joggle_fixed_behavior
    MODULE examples/fixed.joggle
    SOURCES examples/fixed_behavior.cpp)
  set_target_properties(joggle_fixed_behavior PROPERTIES
    PREFIX ""
    OUTPUT_NAME "fixed_behavior"
  )
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
  add_executable(joggle_module_test tests/module_test.cpp)
  target_link_libraries(joggle_module_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_module_test PRIVATE
    JOGGLE_TEST_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/examples/arith.joggle"
  )
  add_test(NAME module COMMAND joggle_module_test)

  add_executable(joggle_type_test tests/type_test.cpp)
  target_link_libraries(joggle_type_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_type_test PRIVATE
    JOGGLE_TEST_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/examples/arith.joggle"
  )
  add_test(NAME type COMMAND joggle_type_test)

  add_executable(joggle_graph_test tests/graph_test.cpp)
  target_link_libraries(joggle_graph_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_graph_test PRIVATE
    JOGGLE_TEST_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/examples/arith.joggle"
  )
  add_test(NAME graph COMMAND joggle_graph_test)

  add_executable(joggle_graph_member_test tests/graph_member_test.cpp)
  target_link_libraries(joggle_graph_member_test PRIVATE joggle::joggle)
  add_test(NAME graph_member COMMAND joggle_graph_member_test)
  add_test(NAME graph_cli
    COMMAND $<TARGET_FILE:joggle_cli> check
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/graph_cli.joggle
  )
  add_test(NAME graph_run_cli
    COMMAND ${CMAKE_COMMAND}
      -DJOGGLE_CLI=$<TARGET_FILE:joggle_cli>
      -DJOGGLE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/graph_cli.joggle
      -DJOGGLE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/graph_run_output.joggle
      -DJOGGLE_BEHAVIOR_SOURCE=${CMAKE_CURRENT_BINARY_DIR}/behavior_plugin.joggle
      -DJOGGLE_BEHAVIOR=$<TARGET_FILE:joggle_test_behavior>
      -DJOGGLE_TARGET=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/graph_target.joggle
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/graph_run_cli.cmake
  )

  add_executable(joggle_behavior_test tests/behavior_test.cpp)
  target_link_libraries(joggle_behavior_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_behavior_test PRIVATE
    JOGGLE_TEST_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/examples/arith.joggle"
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

  add_executable(joggle_pass_test tests/pass_test.cpp)
  target_link_libraries(joggle_pass_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_pass_test PRIVATE
    JOGGLE_ARITH_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/examples/arith.joggle"
    JOGGLE_ARITH_BEHAVIOR="$<TARGET_FILE:joggle_arith_behavior>"
  )
  add_test(NAME pass COMMAND joggle_pass_test)

  add_executable(joggle_tensor_nn_test tests/tensor_nn_test.cpp)
  target_link_libraries(joggle_tensor_nn_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_tensor_nn_test PRIVATE
    JOGGLE_ARITH_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/examples/arith.joggle"
    JOGGLE_TENSOR_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/examples/tensor.joggle"
    JOGGLE_NN_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/examples/nn.joggle"
    JOGGLE_MLP_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/examples/mlp.joggle"
    JOGGLE_FIXED_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/examples/fixed.joggle"
    JOGGLE_ARITH_BEHAVIOR="$<TARGET_FILE:joggle_arith_behavior>"
    JOGGLE_TENSOR_BEHAVIOR="$<TARGET_FILE:joggle_tensor_behavior>"
  )
  add_test(NAME tensor_nn COMMAND joggle_tensor_nn_test)

  add_executable(joggle_edgevec_test tests/edgevec_test.cpp)
  target_link_libraries(joggle_edgevec_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_edgevec_test PRIVATE
    JOGGLE_ARITH_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/examples/arith.joggle"
    JOGGLE_TENSOR_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/examples/tensor.joggle"
    JOGGLE_NN_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/examples/nn.joggle"
    JOGGLE_MLP_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/examples/mlp.joggle"
    JOGGLE_FIXED_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/examples/fixed.joggle"
    JOGGLE_EDGEVEC_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/examples/edgevec.joggle"
    JOGGLE_ARITH_BEHAVIOR="$<TARGET_FILE:joggle_arith_behavior>"
    JOGGLE_TENSOR_BEHAVIOR="$<TARGET_FILE:joggle_tensor_behavior>"
    JOGGLE_FIXED_BEHAVIOR="$<TARGET_FILE:joggle_fixed_behavior>"
    JOGGLE_EDGEVEC_BEHAVIOR="$<TARGET_FILE:joggle_edgevec_behavior>"
  )
  add_test(NAME edgevec COMMAND joggle_edgevec_test)

  add_executable(joggle_fixed_test tests/fixed_test.cpp)
  target_link_libraries(joggle_fixed_test PRIVATE joggle::joggle)
  target_compile_definitions(joggle_fixed_test PRIVATE
    JOGGLE_ARITH_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/examples/arith.joggle"
    JOGGLE_FIXED_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/examples/fixed.joggle"
    JOGGLE_ARITH_BEHAVIOR="$<TARGET_FILE:joggle_arith_behavior>"
    JOGGLE_FIXED_BEHAVIOR="$<TARGET_FILE:joggle_fixed_behavior>"
  )
  add_test(NAME fixed COMMAND joggle_fixed_test)

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
      -DJOGGLE_ARITH=${CMAKE_CURRENT_SOURCE_DIR}/examples/arith.joggle
      -DJOGGLE_TENSOR=${CMAKE_CURRENT_SOURCE_DIR}/examples/tensor.joggle
      -DJOGGLE_NN=${CMAKE_CURRENT_SOURCE_DIR}/examples/nn.joggle
      -DJOGGLE_EDGEVEC=${CMAKE_CURRENT_SOURCE_DIR}/examples/edgevec.joggle
      -DJOGGLE_FIXED=${CMAKE_CURRENT_SOURCE_DIR}/examples/fixed.joggle
      -DJOGGLE_PACKAGE_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-packages
      -DJOGGLE_LOCK_CONSUMER=$<TARGET_FILE:joggle_lock_consumer>
      -DJOGGLE_BEHAVIOR_MODULE=${CMAKE_CURRENT_BINARY_DIR}/behavior_plugin.joggle
      -DJOGGLE_BEHAVIOR_LIBRARY=$<TARGET_FILE:joggle_test_behavior>
      -DJOGGLE_BEHAVIOR_FAILURE=$<TARGET_FILE:joggle_test_behavior_failure>
      -DJOGGLE_BEHAVIOR_LOADER=$<TARGET_FILE:joggle_behavior_loader_test>
      -DJOGGLE_ARITH_BEHAVIOR=$<TARGET_FILE:joggle_arith_behavior>
      -DJOGGLE_TENSOR_BEHAVIOR=$<TARGET_FILE:joggle_tensor_behavior>
      -DJOGGLE_EDGEVEC_BEHAVIOR=$<TARGET_FILE:joggle_edgevec_behavior>
      -DJOGGLE_FIXED_BEHAVIOR=$<TARGET_FILE:joggle_fixed_behavior>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_cli.cmake
  )

  add_test(NAME vertical_cli
    COMMAND ${CMAKE_COMMAND}
      -DJOGGLE_CLI=$<TARGET_FILE:joggle_cli>
      -DJOGGLE_ARITH=${CMAKE_CURRENT_SOURCE_DIR}/examples/arith.joggle
      -DJOGGLE_FIXED=${CMAKE_CURRENT_SOURCE_DIR}/examples/fixed.joggle
      -DJOGGLE_TENSOR=${CMAKE_CURRENT_SOURCE_DIR}/examples/tensor.joggle
      -DJOGGLE_NN=${CMAKE_CURRENT_SOURCE_DIR}/examples/nn.joggle
      -DJOGGLE_MLP=${CMAKE_CURRENT_SOURCE_DIR}/examples/mlp.joggle
      -DJOGGLE_EDGEVEC=${CMAKE_CURRENT_SOURCE_DIR}/examples/edgevec.joggle
      -DJOGGLE_ARITH_BEHAVIOR=$<TARGET_FILE:joggle_arith_behavior>
      -DJOGGLE_FIXED_BEHAVIOR=$<TARGET_FILE:joggle_fixed_behavior>
      -DJOGGLE_TENSOR_BEHAVIOR=$<TARGET_FILE:joggle_tensor_behavior>
      -DJOGGLE_EDGEVEC_BEHAVIOR=$<TARGET_FILE:joggle_edgevec_behavior>
      -DJOGGLE_VERTICAL_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-vertical
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/vertical_cli.cmake
  )
endif()
