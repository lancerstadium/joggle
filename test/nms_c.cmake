set(NAME "NMS")
set(WANT_HEADER ON)

# The declared ABI must carry the dynamic result extent alongside the pointer.
macro(c_pipeline_checks)
  joggle_expect("NMS C ABI omits its dynamic result extent"
    FILE "${header}" MATCHES
    "int64_t\\* out_[0-9]+, int64_t\\* out_[0-9]+_dim_0")
endmacro()

include("${CMAKE_CURRENT_LIST_DIR}/c_pipeline.cmake")
