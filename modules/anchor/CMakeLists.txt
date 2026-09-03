joggle_add_behavior(joggle_module_anchor_behavior
  MODULE module.joggle
  SOURCES src/behavior.cpp)
set_target_properties(joggle_module_anchor_behavior PROPERTIES
  PREFIX ""
  OUTPUT_NAME "anchor")

if(MSVC)
  target_compile_options(joggle_module_anchor_behavior PRIVATE
    /W4 /WX)
else()
  target_compile_options(joggle_module_anchor_behavior PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()

if(JOGGLE_BUILD_TESTS)
  set(joggle_anchor_behavior
    "$<TARGET_FILE:joggle_module_anchor_behavior>")
  set(joggle_anchor_fixture
    "${PROJECT_SOURCE_DIR}/tests/fixtures/resnet18_basic_block.joggle")
  add_executable(joggle_module_anchor_test tests/mapping_test.cpp)
  target_link_libraries(joggle_module_anchor_test PRIVATE
    joggle::joggle)
  if(MSVC)
    target_compile_options(joggle_module_anchor_test PRIVATE
      /W4 /WX)
  else()
    target_compile_options(joggle_module_anchor_test PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
  endif()
  target_compile_definitions(joggle_module_anchor_test PRIVATE
    JOGGLE_ARITH_MODULE="${PROJECT_SOURCE_DIR}/modules/arith/module.joggle"
    JOGGLE_TENSOR_MODULE="${PROJECT_SOURCE_DIR}/modules/tensor/module.joggle"
    JOGGLE_NN_MODULE="${PROJECT_SOURCE_DIR}/modules/nn/module.joggle"
    JOGGLE_MEM_MODULE="${PROJECT_SOURCE_DIR}/modules/mem/module.joggle"
    JOGGLE_ANCHOR_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/module.joggle"
    JOGGLE_ANCHOR_BEHAVIOR="${joggle_anchor_behavior}"
    JOGGLE_RESNET_BLOCK="${joggle_anchor_fixture}")
  add_dependencies(joggle_module_anchor_test joggle_module_anchor_behavior)
  add_test(NAME module.anchor COMMAND joggle_module_anchor_test)

  if(JOGGLE_MODULE_ONNX_ENABLED AND JOGGLE_ONNX_MODEL)
    add_executable(joggle_module_anchor_onnx_test
      tests/onnx_integration_test.cpp)
    target_link_libraries(joggle_module_anchor_onnx_test PRIVATE
      joggle::joggle)
    if(MSVC)
      target_compile_options(joggle_module_anchor_onnx_test PRIVATE
        /W4 /WX)
    else()
      target_compile_options(joggle_module_anchor_onnx_test PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()
    target_compile_definitions(joggle_module_anchor_onnx_test PRIVATE
      JOGGLE_ARITH_MODULE="${PROJECT_SOURCE_DIR}/modules/arith/module.joggle"
      JOGGLE_TENSOR_MODULE="${PROJECT_SOURCE_DIR}/modules/tensor/module.joggle"
      JOGGLE_NN_MODULE="${PROJECT_SOURCE_DIR}/modules/nn/module.joggle"
      JOGGLE_MEM_MODULE="${PROJECT_SOURCE_DIR}/modules/mem/module.joggle"
      JOGGLE_ONNX_MODULE="${PROJECT_SOURCE_DIR}/modules/onnx/module.joggle"
      JOGGLE_ONNX_BEHAVIOR="$<TARGET_FILE:joggle_module_onnx_behavior>"
      JOGGLE_ANCHOR_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/module.joggle"
      JOGGLE_ANCHOR_BEHAVIOR="${joggle_anchor_behavior}"
      JOGGLE_ANCHOR_ONNX_MODEL="${JOGGLE_ONNX_MODEL}")
    add_dependencies(joggle_module_anchor_onnx_test
      joggle_module_onnx_behavior joggle_module_anchor_behavior)
    add_test(NAME module.anchor.onnx COMMAND joggle_module_anchor_onnx_test)
  endif()

  if(JOGGLE_MODULE_ONNX_ENABLED AND JOGGLE_MODULE_PRECISION_ENABLED
      AND JOGGLE_ONNX_MODEL)
    add_executable(joggle_module_anchor_precision_onnx_test
      tests/precision_integration_test.cpp)
    target_link_libraries(joggle_module_anchor_precision_onnx_test PRIVATE
      joggle::joggle)
    if(MSVC)
      target_compile_options(joggle_module_anchor_precision_onnx_test PRIVATE
        /W4 /WX)
    else()
      target_compile_options(joggle_module_anchor_precision_onnx_test PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()
    target_compile_definitions(joggle_module_anchor_precision_onnx_test PRIVATE
      JOGGLE_ARITH_MODULE="${PROJECT_SOURCE_DIR}/modules/arith/module.joggle"
      JOGGLE_TENSOR_MODULE="${PROJECT_SOURCE_DIR}/modules/tensor/module.joggle"
      JOGGLE_NN_MODULE="${PROJECT_SOURCE_DIR}/modules/nn/module.joggle"
      JOGGLE_MEM_MODULE="${PROJECT_SOURCE_DIR}/modules/mem/module.joggle"
      JOGGLE_ONNX_MODULE="${PROJECT_SOURCE_DIR}/modules/onnx/module.joggle"
      JOGGLE_ONNX_BEHAVIOR="$<TARGET_FILE:joggle_module_onnx_behavior>"
      JOGGLE_PRECISION_MODULE="${PROJECT_SOURCE_DIR}/modules/precision/module.joggle"
      JOGGLE_PRECISION_BEHAVIOR="$<TARGET_FILE:joggle_module_precision_behavior>"
      JOGGLE_ANCHOR_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/module.joggle"
      JOGGLE_ANCHOR_BEHAVIOR="${joggle_anchor_behavior}"
      JOGGLE_ANCHOR_PRECISION_ONNX_MODEL="${JOGGLE_ONNX_MODEL}")
    add_dependencies(joggle_module_anchor_precision_onnx_test
      joggle_module_onnx_behavior joggle_module_precision_behavior
      joggle_module_anchor_behavior)
    add_test(NAME module.anchor.precision.onnx
      COMMAND joggle_module_anchor_precision_onnx_test)
  endif()
endif()

install(TARGETS joggle_module_anchor_behavior
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/joggle/behaviors
  RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/joggle/behaviors)
