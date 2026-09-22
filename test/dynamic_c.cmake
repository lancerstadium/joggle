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
  string(JSON api_count LENGTH "${api}")
  math(EXPR api_last "${api_count} - 1")
  set(found_tile OFF)
  foreach(entry RANGE 0 ${api_last})
    string(JSON symbol GET "${api}" ${entry} name)
    if(symbol STREQUAL "dynamic_c_tile_bounded")
      set(found_tile ON)
      string(JSON elements GET "${api}" ${entry} results 0 elements)
      string(JSON dynamic GET "${api}" ${entry} results 0 dynamic)
      if(NOT elements EQUAL 24 OR NOT dynamic)
        message(FATAL_ERROR "dynamic Tile lost its proved 24-element capacity")
      endif()
    endif()
  endforeach()
  if(NOT found_tile)
    message(FATAL_ERROR "dynamic Tile is absent from the C API")
  endif()
endmacro()

include("${CMAKE_CURRENT_LIST_DIR}/c_pipeline.cmake")

foreach(repeats_case a b c)
  joggle_run("invalid tile repeats ${repeats_case} were accepted"
    COMMAND "${program}" "${repeats_case}" EXPECT_FAIL ERROR_VARIABLE failure)
  joggle_expect("invalid tile repeats did not reach validation"
    TEXT "${failure}" MATCHES "[Aa]ssertion")
endforeach()

joggle_run("incompatible dynamic comparison shapes were accepted"
  COMMAND "${program}" "d" EXPECT_FAIL ERROR_VARIABLE failure)
joggle_expect("incompatible comparison did not reach shape validation"
  TEXT "${failure}" MATCHES "[Aa]ssertion")

joggle_run("incompatible dynamic extrema shapes were accepted"
  COMMAND "${program}" "e" EXPECT_FAIL ERROR_VARIABLE failure)
joggle_expect("incompatible extrema did not reach shape validation"
  TEXT "${failure}" MATCHES "[Aa]ssertion")
