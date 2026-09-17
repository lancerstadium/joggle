set(NAME "dynamic")
set(WANT_HEADER ON)

# Two symbols carry a data-dependent extent. The declared header, the recorded
# API metadata, and the emitted source must agree that the extent is explicit
# rather than implied by a runtime storage slot.
macro(c_pipeline_checks)
  joggle_expect("dynamic C header omits its flat output ABI"
    FILE "${header}" MATCHES
    "void dynamic_c_prefix\\(int32_t\\* out, int64_t\\* out_dim_0\\);")
  joggle_expect("dynamic C header omits its explicit input extent"
    FILE "${header}" MATCHES
    "int32_t dynamic_c_sum\\(const int32_t\\* x, int64_t x_dim_0\\);")
  joggle_expect("dynamic C API capacity metadata is incomplete"
    TEXT "${api}" MATCHES "\\\"capacity\\\": \\[4, 2\\]")
  joggle_expect("dynamic C API shape metadata is incomplete"
    TEXT "${api}" MATCHES "\\\"shape\\\": \\[\\\"_\\\", 2\\]")
  joggle_expect("dynamic C API omits the input extent"
    TEXT "${api}" MATCHES "x_dim_0")
  joggle_expect("dynamic C ABI is not flat and explicit"
    FILE "${source}" MATCHES
    "void dynamic_c_prefix\\(int32_t\\* out, int64_t\\* out_dim_0\\)")
  joggle_expect("dynamic C input extents are not explicit"
    FILE "${source}" MATCHES
    "int32_t dynamic_c_sum\\(const int32_t\\* x, int64_t x_dim_0\\)")
endmacro()

include("${CMAKE_CURRENT_LIST_DIR}/c_pipeline.cmake")
