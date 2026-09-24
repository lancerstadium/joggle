include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

joggle_require("bounds test requires TOOL, models, MODULES, and ROOT" VARS TOOL MODEL FOLD_MODEL CUSTOM_MODEL MODULES ROOT)

joggle_workspace("${ROOT}")
set(bounded "${ROOT}/bounded.jog")
set(stable "${ROOT}/stable.jog")
set(folded "${ROOT}/folded.jog")
set(custom "${ROOT}/custom.jog")

joggle_run("bounds query failed"
  COMMAND "${TOOL}" query bounds.report "${MODEL}" -M "${MODULES}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
foreach(pair
    "i8;-1" "u8;255" "i16;-32768" "u16;65535"
    "i32;-2147483648" "u32;4294967295"
    "i64_min;-9223372036854775808" "i64_max;9223372036854775807"
    "u64_fit;9223372036854775807")
  list(GET pair 0 name)
  list(GET pair 1 value)
  joggle_expect("binary ${name} element lost its exact bound"
    TEXT "${output}" MATCHES
    "\"hi\": ${value}, \"lo\": ${value}, \"name\": \"binary_${name}\"")
endforeach()
foreach(limit 10 30)
  joggle_expect("nested calls lost argument-local bounds"
    TEXT "${output}" MATCHES
    "\"hi\": ${limit}, \"lo\": -2147483648, \"name\": \"nested${limit}\"")
  joggle_expect("indexed calls lost their scalar argument"
    TEXT "${output}" MATCHES
    "\"hi\": ${limit}, \"lo\": ${limit}, \"name\": \"indexed${limit}\"")
endforeach()
joggle_expect("binary analysis guessed an unrepresentable or unknown element"
  TEXT "${output}" NOT_MATCHES
  "\"name\": \"binary_(u64|index|oob)_unknown\"")
if(NOT output MATCHES
   "\"[0-9]+\": \\{\"fn\": \"kernel\", \"hi\": 5, \"lo\": 2, \"name\": \"i\", \"type\": \"index\"\\}" OR
   NOT output MATCHES
   "\"[0-9]+\": \\{\"fn\": \"kernel\", \"hi\": 19, \"lo\": 7, \"name\": \"j\", \"type\": \"index\"\\}" OR
   NOT output MATCHES
   "\"[0-9]+\": \\{\"fn\": \"choose\", \"hi\": 8, \"lo\": -2, \"name\": \"selected\", \"type\": \"int\"\\}" OR
   output MATCHES "\"name\": \"wrapped\"" OR
   output MATCHES "\"name\": \"product\"")
  message(FATAL_ERROR "unexpected integer bounds report:\n${output}")
endif()

joggle_expect("shape-vector interval was lost"
  TEXT "${output}" MATCHES
  "\"hi\": 2, \"lo\": 0, \"name\": \"bounded_element\"")
joggle_expect("upper clamp lost its comparison constraint"
  TEXT "${output}" MATCHES
  "\"hi\": 100, \"lo\": -2147483648, \"name\": \"upper_clamped\"")
joggle_expect("first call lost its argument-specific bound"
  TEXT "${output}" MATCHES
  "\"hi\": 10, \"lo\": -2147483648, \"name\": \"call10\"")
joggle_expect("second call reused another call's bound"
  TEXT "${output}" MATCHES
  "\"hi\": 30, \"lo\": -2147483648, \"name\": \"call30\"")
joggle_expect("recursive call acquired an unsupported finite summary"
  TEXT "${output}" NOT_MATCHES "\"name\": \"recursive_result\"")
joggle_expect("reversed comparison lost its lower constraint"
  TEXT "${output}" MATCHES
  "\"hi\": 2147483647, \"lo\": -5, \"name\": \"lower_clamped\"")
joggle_expect("false branch lost its inverted constraint"
  TEXT "${output}" MATCHES
  "\"hi\": 100, \"lo\": -2147483648, \"name\": \"else_clamped\"")
joggle_expect("equality branch lost its exact constraint"
  TEXT "${output}" MATCHES
  "\"hi\": 100, \"lo\": 100, \"name\": \"equal_clamped\"")
joggle_expect("branch constraint leaked to an unguarded input use"
  TEXT "${output}" NOT_MATCHES "\"name\": \"outside_branch\"")
joggle_expect("unchanged shape-vector element was lost"
  TEXT "${output}" MATCHES
  "\"hi\": 7, \"lo\": 7, \"name\": \"untouched_element\"")
joggle_expect("static axis of a partially dynamic tensor was lost"
  TEXT "${output}" MATCHES
  "\"hi\": 3, \"lo\": 3, \"name\": \"fixed_extent\"")
if(output MATCHES "\"name\": \"uncertain_element\"|\"name\": \"unknown_extent\"")
  message(FATAL_ERROR "bounds guessed through an unknown store or extent")
endif()

joggle_run("bounds folding failed"
  COMMAND "${TOOL}" run bounds.fold "${FOLD_MODEL}" -M "${MODULES}"
  OUTPUT_FILE "${bounded}"
  ERROR_VARIABLE error)
file(READ "${bounded}" text)
if(text MATCHES "if i >= 2" OR NOT text MATCHES "if j < 10" OR
   text MATCHES "tensor.dim" OR NOT text MATCHES "return 3")
  message(FATAL_ERROR
          "bounds folding changed the wrong conditions:\n${text}")
endif()
joggle_run("repeated bounds folding failed"
  COMMAND "${TOOL}" run bounds.fold "${bounded}" -M "${MODULES}"
  OUTPUT_FILE "${stable}"
  ERROR_VARIABLE error)
joggle_run("bounds folding is not idempotent"
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${bounded}" "${stable}")
joggle_run("custom bounds folding failed"
  COMMAND "${TOOL}" run bounds.fold "${CUSTOM_MODEL}" -M "${MODULES}"
  OUTPUT_FILE "${custom}"
  ERROR_VARIABLE error)
file(READ "${custom}" text)
if(NOT text MATCHES "return i32\\(1\\) < i32\\(2\\)")
  message(FATAL_ERROR
          "bounds folding assumed semantics for a user overload:\n${text}")
endif()
joggle_run("bounds cleanup failed"
  COMMAND "${TOOL}" run opt.fold opt.basic "${bounded}" -M "${MODULES}"
  OUTPUT_FILE "${folded}"
  ERROR_VARIABLE error)
file(READ "${folded}" text)
if(NOT text MATCHES "var total = 14" OR
   NOT text MATCHES "var rejected = 14" OR
   text MATCHES "if i >= 2|if i == 9")
  message(FATAL_ERROR
          "bounds facts did not compose with ordinary folding:\n${text}")
endif()
