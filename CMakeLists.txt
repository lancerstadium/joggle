cmake_minimum_required(VERSION 3.20)

project(Joggle VERSION 0.1.0 LANGUAGES CXX)

option(JOGGLE_BUILD_TESTS "Build Joggle tests" ON)

add_library(joggle src/env.cpp src/ir.cpp src/lang.cpp)
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

add_executable(joggle-tool tool/main.cpp)
set_target_properties(joggle-tool PROPERTIES OUTPUT_NAME joggle)
target_link_libraries(joggle-tool PRIVATE Joggle::joggle)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS joggle joggle-tool EXPORT JoggleTargets)
install(FILES include/joggle/joggle.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/joggle)
install(DIRECTORY modules/ DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules)
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
  add_executable(joggle-workflow-test test/workflow.cpp)
  target_link_libraries(joggle-workflow-test PRIVATE Joggle::joggle)
  add_test(
    NAME workflow
    COMMAND joggle-workflow-test
            ${CMAKE_CURRENT_SOURCE_DIR}/test/data/matmul.jog
            ${CMAKE_CURRENT_SOURCE_DIR}/modules
            ${CMAKE_CURRENT_BINARY_DIR}/modules
  )
endif()
