cmake_minimum_required(VERSION 3.20)

project(JoggleConsumer LANGUAGES CXX)

find_package(Joggle CONFIG REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE Joggle::joggle)
add_library(probe MODULE native.cpp)
target_link_libraries(probe PRIVATE Joggle::joggle)
set_target_properties(
  probe
  PROPERTIES
    PREFIX ""
    OUTPUT_NAME "joggle_probe"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/package/native"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/package/native"
)
configure_file(module.jog "${CMAKE_BINARY_DIR}/package/module.jog" COPYONLY)
set_target_properties(
  consumer
  PROPERTIES
    CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${config}" upper)
  set_target_properties(
    consumer
    PROPERTIES
      "RUNTIME_OUTPUT_DIRECTORY_${upper}" "${CMAKE_BINARY_DIR}/bin"
  )
  set_target_properties(
    probe
    PROPERTIES
      "LIBRARY_OUTPUT_DIRECTORY_${upper}" "${CMAKE_BINARY_DIR}/package/native"
      "RUNTIME_OUTPUT_DIRECTORY_${upper}" "${CMAKE_BINARY_DIR}/package/native"
  )
endforeach()
