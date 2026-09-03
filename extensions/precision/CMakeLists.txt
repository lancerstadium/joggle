joggle_add_behavior(joggle_precision_behavior
  MODULE precision.joggle
  SOURCES behavior.cpp)
set_target_properties(joggle_precision_behavior PROPERTIES
  PREFIX ""
  OUTPUT_NAME "precision")
if(MSVC)
  target_compile_options(joggle_precision_behavior PRIVATE /W4 /WX)
else()
  target_compile_options(joggle_precision_behavior PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()

if(JOGGLE_BUILD_TESTS)
  add_executable(joggle_precision_test test.cpp)
  target_link_libraries(joggle_precision_test PRIVATE joggle::joggle)
  if(MSVC)
    target_compile_options(joggle_precision_test PRIVATE /W4 /WX)
  else()
    target_compile_options(joggle_precision_test PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
  endif()
  target_compile_definitions(joggle_precision_test PRIVATE
    JOGGLE_ARITH_MODULE="${PROJECT_SOURCE_DIR}/modules/arith.joggle"
    JOGGLE_RESOURCE_MODULE="${PROJECT_SOURCE_DIR}/modules/resource.joggle"
    JOGGLE_TENSOR_MODULE="${PROJECT_SOURCE_DIR}/modules/tensor.joggle"
    JOGGLE_NN_MODULE="${PROJECT_SOURCE_DIR}/modules/nn.joggle"
    JOGGLE_PRECISION_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/precision.joggle"
    JOGGLE_PRECISION_BEHAVIOR="$<TARGET_FILE:joggle_precision_behavior>")
  add_dependencies(joggle_precision_test joggle_precision_behavior)
  add_test(NAME precision COMMAND joggle_precision_test)

  if(JOGGLE_BUILD_ONNX AND JOGGLE_ONNX_MODEL)
    add_executable(joggle_precision_onnx_test onnx_test.cpp)
    target_link_libraries(joggle_precision_onnx_test PRIVATE joggle::joggle)
    if(MSVC)
      target_compile_options(joggle_precision_onnx_test PRIVATE /W4 /WX)
    else()
      target_compile_options(joggle_precision_onnx_test PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()
    target_compile_definitions(joggle_precision_onnx_test PRIVATE
      JOGGLE_ARITH_MODULE="${PROJECT_SOURCE_DIR}/modules/arith.joggle"
      JOGGLE_RESOURCE_MODULE="${PROJECT_SOURCE_DIR}/modules/resource.joggle"
      JOGGLE_TENSOR_MODULE="${PROJECT_SOURCE_DIR}/modules/tensor.joggle"
      JOGGLE_NN_MODULE="${PROJECT_SOURCE_DIR}/modules/nn.joggle"
      JOGGLE_ONNX_MODULE="${PROJECT_SOURCE_DIR}/extensions/onnx/onnx.joggle"
      JOGGLE_ONNX_BEHAVIOR="$<TARGET_FILE:joggle_onnx_behavior>"
      JOGGLE_PRECISION_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/precision.joggle"
      JOGGLE_PRECISION_BEHAVIOR="$<TARGET_FILE:joggle_precision_behavior>"
      JOGGLE_PRECISION_ONNX_MODEL="${JOGGLE_ONNX_MODEL}")
    add_dependencies(joggle_precision_onnx_test
      joggle_onnx_behavior joggle_precision_behavior)
    add_test(NAME precision_onnx COMMAND joggle_precision_onnx_test)
  endif()
endif()

install(FILES precision.joggle
  DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules)
install(TARGETS joggle_precision_behavior
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/joggle/behaviors
  RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/joggle/behaviors)
