set(NAME "TopK")

# A shape-only result must not survive into emitted C as a runtime storage slot.
macro(c_pipeline_checks)
  joggle_expect("TopK C retains a shape-only runtime storage slot"
    FILE "${source}" NOT_MATCHES "slot_index_[0-9]+\\[")
endmacro()

include("${CMAKE_CURRENT_LIST_DIR}/c_pipeline.cmake")
