cmake_minimum_required(VERSION 3.20)

project(JoggleConsumer LANGUAGES CXX)

find_package(Joggle CONFIG REQUIRED)

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/module-dir.txt"
  CONTENT "${Joggle_MODULE_DIR}\n")

joggle_add_behavior(joggle_consumer_behavior
  MODULE module.joggle
  SOURCES behavior.cpp
)
set_target_properties(joggle_consumer_behavior PROPERTIES
  PREFIX ""
  OUTPUT_NAME "external_behavior"
)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/behavior-path.txt"
  CONTENT "$<TARGET_FILE:joggle_consumer_behavior>")

add_executable(joggle_consumer main.cpp)
target_link_libraries(joggle_consumer PRIVATE joggle::joggle)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/consumer-path.txt"
  CONTENT "$<TARGET_FILE:joggle_consumer>")
