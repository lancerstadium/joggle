# Exercise ONNX conversion, Jog serialization, C lowering, and execution.
set(NAME "Loop")
set(CONVERT ON)
set(WANT_HEADER ON)
include("${CMAKE_CURRENT_LIST_DIR}/c_pipeline.cmake")

foreach(shape_case a b c d e f)
  joggle_run("invalid reshape ${shape_case} was accepted"
    COMMAND "${program}" "${shape_case}" EXPECT_FAIL ERROR_VARIABLE failure)
  joggle_expect("invalid reshape did not reach its validation assertion"
    TEXT "${failure}" MATCHES "[Aa]ssertion")
endforeach()
