joggle_add_behavior(joggle_module_reference_target_behavior
  MODULE module.joggle
  SOURCES src/behavior.cpp)
set_target_properties(joggle_module_reference_target_behavior PROPERTIES
  PREFIX ""
  OUTPUT_NAME "reference_target")

if(MSVC)
  target_compile_options(joggle_module_reference_target_behavior PRIVATE
    /W4 /WX)
else()
  target_compile_options(joggle_module_reference_target_behavior PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()

if(JOGGLE_BUILD_TESTS)
  set(joggle_reference_target_behavior
    "$<TARGET_FILE:joggle_module_reference_target_behavior>")
  set(joggle_reference_target_fixture
    "${PROJECT_SOURCE_DIR}/tests/fixtures/resnet18_basic_block.joggle")
  add_executable(joggle_module_reference_target_test tests/mapping_test.cpp)
  target_link_libraries(joggle_module_reference_target_test PRIVATE
    joggle::joggle)
  if(MSVC)
    target_compile_options(joggle_module_reference_target_test PRIVATE
      /W4 /WX)
  else()
    target_compile_options(joggle_module_reference_target_test PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
  endif()
  target_compile_definitions(joggle_module_reference_target_test PRIVATE
    JOGGLE_ARITH_MODULE="${PROJECT_SOURCE_DIR}/modules/arith/module.joggle"
    JOGGLE_TENSOR_MODULE="${PROJECT_SOURCE_DIR}/modules/tensor/module.joggle"
    JOGGLE_NN_MODULE="${PROJECT_SOURCE_DIR}/modules/nn/module.joggle"
    JOGGLE_MEM_MODULE="${PROJECT_SOURCE_DIR}/modules/mem/module.joggle"
    JOGGLE_REFERENCE_TARGET_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/module.joggle"
    JOGGLE_REFERENCE_TARGET_BEHAVIOR="${joggle_reference_target_behavior}"
    JOGGLE_RESNET_BLOCK="${joggle_reference_target_fixture}")
  add_dependencies(joggle_module_reference_target_test
    joggle_module_reference_target_behavior)
  add_test(NAME module.reference_target
    COMMAND joggle_module_reference_target_test)

  if(JOGGLE_MODULE_ONNX_ENABLED AND JOGGLE_ONNX_MODEL)
    add_executable(joggle_module_reference_target_onnx_test
      tests/onnx_integration_test.cpp)
    target_link_libraries(joggle_module_reference_target_onnx_test PRIVATE
      joggle::joggle)
    if(MSVC)
      target_compile_options(joggle_module_reference_target_onnx_test PRIVATE
        /W4 /WX)
    else()
      target_compile_options(joggle_module_reference_target_onnx_test PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()
    target_compile_definitions(joggle_module_reference_target_onnx_test PRIVATE
      JOGGLE_ARITH_MODULE="${PROJECT_SOURCE_DIR}/modules/arith/module.joggle"
      JOGGLE_TENSOR_MODULE="${PROJECT_SOURCE_DIR}/modules/tensor/module.joggle"
      JOGGLE_NN_MODULE="${PROJECT_SOURCE_DIR}/modules/nn/module.joggle"
      JOGGLE_MEM_MODULE="${PROJECT_SOURCE_DIR}/modules/mem/module.joggle"
      JOGGLE_ONNX_MODULE="${PROJECT_SOURCE_DIR}/modules/onnx/module.joggle"
      JOGGLE_ONNX_BEHAVIOR="$<TARGET_FILE:joggle_module_onnx_behavior>"
      JOGGLE_REFERENCE_TARGET_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/module.joggle"
      JOGGLE_REFERENCE_TARGET_BEHAVIOR="${joggle_reference_target_behavior}"
      JOGGLE_REFERENCE_TARGET_ONNX_MODEL="${JOGGLE_ONNX_MODEL}")
    add_dependencies(joggle_module_reference_target_onnx_test
      joggle_module_onnx_behavior joggle_module_reference_target_behavior)
    add_test(NAME module.reference_target.onnx
      COMMAND joggle_module_reference_target_onnx_test)
  endif()
endif()

install(TARGETS joggle_module_reference_target_behavior
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/joggle/behaviors
  RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/joggle/behaviors)
