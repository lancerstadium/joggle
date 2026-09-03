joggle_add_behavior(joggle_module_precision_behavior
  MODULE module.joggle
  SOURCES src/behavior.cpp)
set_target_properties(joggle_module_precision_behavior PROPERTIES
  PREFIX ""
  OUTPUT_NAME "precision")
if(MSVC)
  target_compile_options(joggle_module_precision_behavior PRIVATE /W4 /WX)
else()
  target_compile_options(joggle_module_precision_behavior PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()

if(JOGGLE_BUILD_TESTS)
  add_executable(joggle_module_precision_test tests/transform_test.cpp)
  target_link_libraries(joggle_module_precision_test PRIVATE joggle::joggle)
  if(MSVC)
    target_compile_options(joggle_module_precision_test PRIVATE /W4 /WX)
  else()
    target_compile_options(joggle_module_precision_test PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
  endif()
  target_compile_definitions(joggle_module_precision_test PRIVATE
    JOGGLE_ARITH_MODULE="${PROJECT_SOURCE_DIR}/modules/arith/module.joggle"
    JOGGLE_TENSOR_MODULE="${PROJECT_SOURCE_DIR}/modules/tensor/module.joggle"
    JOGGLE_NN_MODULE="${PROJECT_SOURCE_DIR}/modules/nn/module.joggle"
    JOGGLE_PRECISION_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/module.joggle"
    JOGGLE_PRECISION_BEHAVIOR="$<TARGET_FILE:joggle_module_precision_behavior>")
  add_dependencies(joggle_module_precision_test
    joggle_module_precision_behavior)
  add_test(NAME module.precision COMMAND joggle_module_precision_test)

  if(JOGGLE_MODULE_ONNX_ENABLED AND JOGGLE_ONNX_MODEL)
    add_executable(joggle_module_precision_onnx_test
      tests/onnx_integration_test.cpp)
    target_link_libraries(joggle_module_precision_onnx_test PRIVATE
      joggle::joggle)
    if(MSVC)
      target_compile_options(joggle_module_precision_onnx_test PRIVATE
        /W4 /WX)
    else()
      target_compile_options(joggle_module_precision_onnx_test PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()
    target_compile_definitions(joggle_module_precision_onnx_test PRIVATE
      JOGGLE_ARITH_MODULE="${PROJECT_SOURCE_DIR}/modules/arith/module.joggle"
      JOGGLE_TENSOR_MODULE="${PROJECT_SOURCE_DIR}/modules/tensor/module.joggle"
      JOGGLE_NN_MODULE="${PROJECT_SOURCE_DIR}/modules/nn/module.joggle"
      JOGGLE_ONNX_MODULE="${PROJECT_SOURCE_DIR}/modules/onnx/module.joggle"
      JOGGLE_ONNX_BEHAVIOR="$<TARGET_FILE:joggle_module_onnx_behavior>"
      JOGGLE_PRECISION_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/module.joggle"
      JOGGLE_PRECISION_BEHAVIOR="$<TARGET_FILE:joggle_module_precision_behavior>"
      JOGGLE_PRECISION_ONNX_MODEL="${JOGGLE_ONNX_MODEL}")
    add_dependencies(joggle_module_precision_onnx_test
      joggle_module_onnx_behavior joggle_module_precision_behavior)
    add_test(NAME module.precision.onnx
      COMMAND joggle_module_precision_onnx_test)
  endif()
endif()

install(TARGETS joggle_module_precision_behavior
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/joggle/behaviors
  RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/joggle/behaviors)
