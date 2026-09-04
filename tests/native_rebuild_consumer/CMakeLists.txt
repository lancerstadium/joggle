cmake_minimum_required(VERSION 3.20)
project(JoggleModRebuild LANGUAGES CXX)

if(NOT DEFINED JOGGLE_NATIVE_SOURCE)
  message(FATAL_ERROR "JOGGLE_NATIVE_SOURCE was not provided")
endif()

find_package(Joggle CONFIG REQUIRED)

joggle_mod(rebuild_native
  SOURCE mod.joggle
  NATIVE "${JOGGLE_NATIVE_SOURCE}"
)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/native-path.txt"
  CONTENT "$<TARGET_FILE:rebuild_native>")
