set(NAME "Range")

macro(c_pipeline_checks)
  joggle_expect("Range fixture no longer exercises a shared helper"
    FILE "${prepared}" MATCHES "local fn minimum\\(")
  string(JSON api_count LENGTH "${api}")
  math(EXPR api_last "${api_count} - 1")
  set(capped_results 0)
  foreach(entry RANGE 0 ${api_last})
    string(JSON symbol GET "${api}" ${entry} name)
    if(symbol STREQUAL "range_c_capped" OR symbol STREQUAL "range_c_descending")
      string(JSON capacity GET "${api}" ${entry} results 0 elements)
      if(capacity LESS 100 OR capacity GREATER 101)
        message(FATAL_ERROR "bounded Range has an imprecise capacity: ${capacity}")
      endif()
      math(EXPR capped_results "${capped_results} + 1")
    endif()
  endforeach()
  if(NOT capped_results EQUAL 2)
    message(FATAL_ERROR "bounded Range results are missing")
  endif()
  joggle_expect("Range C capacity metadata"
    TEXT "${api}" MATCHES "\\\"capacity\\\": \\[4\\]")
  joggle_expect("Range C shape metadata"
    TEXT "${api}" MATCHES "\\\"shape\\\": \\[\\\"_\\\"\\]")
endmacro()

include("${CMAKE_CURRENT_LIST_DIR}/c_pipeline.cmake")
