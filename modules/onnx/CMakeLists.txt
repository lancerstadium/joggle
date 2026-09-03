find_package(Protobuf CONFIG REQUIRED)

set(JOGGLE_ONNX_PROTO "" CACHE FILEPATH
  "Path to an ONNX onnx.proto3 file; empty downloads the pinned schema")
set(joggle_onnx_proto "${CMAKE_CURRENT_BINARY_DIR}/onnx.proto3")
if(NOT JOGGLE_ONNX_PROTO AND NOT EXISTS "${joggle_onnx_proto}")
  file(DOWNLOAD
    "https://raw.githubusercontent.com/onnx/onnx/2bb50465112feca9003e1ed654d77f01ff1415ca/onnx/onnx.proto3"
    "${joggle_onnx_proto}"
    EXPECTED_HASH
      "SHA256=8b298f974c10d5efcbfcab0612719f9f543c7c9aafca5c6aeee4c4d1ee8dbef2"
    TLS_VERIFY ON
    STATUS joggle_onnx_download)
  list(GET joggle_onnx_download 0 joggle_onnx_download_code)
  list(GET joggle_onnx_download 1 joggle_onnx_download_message)
  if(NOT joggle_onnx_download_code EQUAL 0)
    message(FATAL_ERROR
      "Cannot download the pinned ONNX schema: ${joggle_onnx_download_message}")
  endif()
elseif(JOGGLE_ONNX_PROTO AND NOT EXISTS "${JOGGLE_ONNX_PROTO}")
  message(FATAL_ERROR "JOGGLE_ONNX_PROTO does not exist: ${JOGGLE_ONNX_PROTO}")
elseif(JOGGLE_ONNX_PROTO)
  configure_file("${JOGGLE_ONNX_PROTO}" "${joggle_onnx_proto}" COPYONLY)
endif()

set(joggle_onnx_generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${joggle_onnx_generated}")
add_custom_command(
  OUTPUT
    "${joggle_onnx_generated}/onnx.proto3.pb.cc"
    "${joggle_onnx_generated}/onnx.proto3.pb.h"
  COMMAND protobuf::protoc
    "--proto_path=${CMAKE_CURRENT_BINARY_DIR}"
    "--cpp_out=${joggle_onnx_generated}"
    "${joggle_onnx_proto}"
  DEPENDS "${joggle_onnx_proto}" protobuf::protoc
  VERBATIM)

add_library(joggle_module_onnx_proto STATIC
  "${joggle_onnx_generated}/onnx.proto3.pb.cc")
set_target_properties(joggle_module_onnx_proto PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
target_link_libraries(joggle_module_onnx_proto PUBLIC protobuf::libprotobuf)
target_include_directories(joggle_module_onnx_proto PUBLIC
  "${joggle_onnx_generated}")

joggle_add_behavior(joggle_module_onnx_behavior
  MODULE module.joggle
  SOURCES src/behavior.cpp)
set_target_properties(joggle_module_onnx_behavior PROPERTIES
  PREFIX ""
  OUTPUT_NAME "onnx")
target_link_libraries(joggle_module_onnx_behavior PRIVATE
  joggle_module_onnx_proto)
if(MSVC)
  target_compile_options(joggle_module_onnx_behavior PRIVATE /W4 /WX)
else()
  target_compile_options(joggle_module_onnx_behavior PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()

if(JOGGLE_BUILD_TESTS)
  set(JOGGLE_ONNX_MODEL "" CACHE FILEPATH
    "Optional ONNX Model Zoo ResNet-18 file for the full import test")
  add_executable(joggle_module_onnx_test tests/import_test.cpp)
  target_link_libraries(joggle_module_onnx_test PRIVATE
    joggle::joggle
    joggle_module_onnx_proto)
  if(MSVC)
    target_compile_options(joggle_module_onnx_test PRIVATE /W4 /WX)
  else()
    target_compile_options(joggle_module_onnx_test PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
  endif()
  target_compile_definitions(joggle_module_onnx_test PRIVATE
    JOGGLE_ARITH_MODULE="${PROJECT_SOURCE_DIR}/modules/arith/module.joggle"
    JOGGLE_TENSOR_MODULE="${PROJECT_SOURCE_DIR}/modules/tensor/module.joggle"
    JOGGLE_NN_MODULE="${PROJECT_SOURCE_DIR}/modules/nn/module.joggle"
    JOGGLE_ONNX_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/module.joggle"
    JOGGLE_ONNX_BEHAVIOR="$<TARGET_FILE:joggle_module_onnx_behavior>")
  add_dependencies(joggle_module_onnx_test joggle_module_onnx_behavior)
  add_test(NAME module.onnx COMMAND joggle_module_onnx_test)
  if(JOGGLE_ONNX_MODEL)
    if(NOT EXISTS "${JOGGLE_ONNX_MODEL}")
      message(FATAL_ERROR
        "JOGGLE_ONNX_MODEL does not exist: ${JOGGLE_ONNX_MODEL}")
    endif()
    add_test(NAME module.onnx.model
      COMMAND joggle_module_onnx_test "${JOGGLE_ONNX_MODEL}")
  endif()
endif()

install(TARGETS joggle_module_onnx_behavior
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/joggle/behaviors
  RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/joggle/behaviors)
