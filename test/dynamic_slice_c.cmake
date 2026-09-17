set(NAME "dynamic Slice")

macro(c_pipeline_checks)
  joggle_expect("dynamic Slice C capacity metadata"
    TEXT "${api}" MATCHES "\\\"capacity\\\": \\[4, 5\\]")
  joggle_expect("dynamic Slice C shape metadata"
    TEXT "${api}" MATCHES "\\\"shape\\\": \\[\\\"_\\\", \\\"_\\\"\\]")
endmacro()

include("${CMAKE_CURRENT_LIST_DIR}/c_pipeline.cmake")
