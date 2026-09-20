# The quantize-dequantize fixture is an ONNX model, so it reaches c.prepare
# through the frontend; the shared pipeline is otherwise enough.
set(NAME "QLinear")
set(CONVERT ON)
set(LINK_LIBRARIES -lm)
include("${CMAKE_CURRENT_LIST_DIR}/c_pipeline.cmake")
