cmake_minimum_required(VERSION 3.20)

project(Joggle VERSION 0.1.0 LANGUAGES CXX)

option(JOGGLE_BUILD_TESTS "Build Joggle tests" ON)

add_library(joggle src/joggle.cpp)
add_library(Joggle::joggle ALIAS joggle)

target_compile_features(joggle PUBLIC cxx_std_20)
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
install(TARGETS joggle joggle-tool EXPORT JoggleTargets)
install(FILES include/joggle/joggle.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/joggle)
install(
  EXPORT JoggleTargets
  NAMESPACE Joggle::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Joggle
)

if(JOGGLE_BUILD_TESTS)
  enable_testing()
  add_executable(joggle-workflow-test test/workflow.cpp)
  target_link_libraries(joggle-workflow-test PRIVATE Joggle::joggle)
  add_test(NAME workflow COMMAND joggle-workflow-test)
endif()
