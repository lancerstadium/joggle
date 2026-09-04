cmake_minimum_required(VERSION 3.20)

project(JoggleConsumer LANGUAGES CXX)

find_package(Joggle CONFIG REQUIRED)

joggle_module(joggle_consumer_native
  SOURCE module.joggle
  NATIVE native.cpp
)
set_target_properties(joggle_consumer_native PROPERTIES
  PREFIX ""
  OUTPUT_NAME "external_native"
)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/native-path.txt"
  CONTENT "$<TARGET_FILE:joggle_consumer_native>")

add_executable(joggle_consumer main.cpp)
target_link_libraries(joggle_consumer PRIVATE joggle::joggle)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/consumer-path.txt"
  CONTENT "$<TARGET_FILE:joggle_consumer>")
