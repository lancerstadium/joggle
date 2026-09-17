set(NAME "NonZero")

# The declared API must record the result capacity and keep the leading axis
# symbolic, because NonZero's extent is data dependent.
macro(c_pipeline_checks)
  joggle_expect("NonZero C capacity metadata"
    TEXT "${api}" MATCHES "\\\"capacity\\\": \\[2, 6\\]")
  joggle_expect("NonZero C shape metadata"
    TEXT "${api}" MATCHES "\\\"shape\\\": \\[2, \\\"_\\\"\\]")
endmacro()

include("${CMAKE_CURRENT_LIST_DIR}/c_pipeline.cmake")
