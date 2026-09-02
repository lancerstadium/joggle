cmake_minimum_required(VERSION 3.20)
project(JoggleBehaviorRebuild LANGUAGES CXX)

if(NOT DEFINED JOGGLE_BEHAVIOR_SOURCE)
  message(FATAL_ERROR "JOGGLE_BEHAVIOR_SOURCE was not provided")
endif()

find_package(Joggle CONFIG REQUIRED)

joggle_add_behavior(rebuild_behavior
  MODULE module.joggle
  SOURCES "${JOGGLE_BEHAVIOR_SOURCE}"
)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/behavior-path.txt"
  CONTENT "$<TARGET_FILE:rebuild_behavior>")
