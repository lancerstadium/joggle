joggle_add_behavior(joggle_example_nn_pipeline_behavior
  MODULE module.joggle
  SOURCES src/behavior.cpp)
set_target_properties(joggle_example_nn_pipeline_behavior PROPERTIES
  PREFIX ""
  OUTPUT_NAME "nn_pipeline")

add_executable(joggle_example_nn_pipeline_test tests/pipeline_test.cpp)
target_link_libraries(joggle_example_nn_pipeline_test PRIVATE joggle::joggle)
target_compile_definitions(joggle_example_nn_pipeline_test PRIVATE
  JOGGLE_ARITH_MODULE="${PROJECT_SOURCE_DIR}/modules/arith/module.joggle"
  JOGGLE_TENSOR_MODULE="${PROJECT_SOURCE_DIR}/modules/tensor/module.joggle"
  JOGGLE_NN_MODULE="${PROJECT_SOURCE_DIR}/modules/nn/module.joggle"
  JOGGLE_RESNET_BLOCK="${PROJECT_SOURCE_DIR}/tests/fixtures/resnet18_basic_block.joggle"
  JOGGLE_EXAMPLE_ACCEL_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/../example_accel/module.joggle"
  JOGGLE_NN_PIPELINE_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/module.joggle"
  JOGGLE_NN_PIPELINE_BEHAVIOR="$<TARGET_FILE:joggle_example_nn_pipeline_behavior>")
add_test(NAME example.nn_pipeline COMMAND joggle_example_nn_pipeline_test)
