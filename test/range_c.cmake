set(NAME "Range")

macro(c_pipeline_checks)
  joggle_expect("Range C capacity metadata"
    TEXT "${api}" MATCHES "\\\"capacity\\\": \\[4\\]")
  joggle_expect("Range C shape metadata"
    TEXT "${api}" MATCHES "\\\"shape\\\": \\[\\\"_\\\"\\]")
endmacro()

include("${CMAKE_CURRENT_LIST_DIR}/c_pipeline.cmake")
