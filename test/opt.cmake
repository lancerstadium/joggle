include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

joggle_require("opt test requires TOOL, MODEL, CSE_MODEL, and MODULES" VARS TOOL MODEL CSE_MODEL MODULES)

joggle_run("indexed CSE failed"
  COMMAND "${TOOL}" run opt.basic "${CSE_MODEL}" -M "${MODULES}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
string(REGEX MATCHALL "x \\+ i32\\(1\\)" additions "${output}")
list(LENGTH additions addition_count)
string(REGEX MATCHALL "x \\+ one" shared_additions "${output}")
list(LENGTH shared_additions shared_count)
if(NOT addition_count EQUAL 2 OR NOT shared_count EQUAL 1 OR
   output MATCHES "let second =")
  message(FATAL_ERROR
          "CSE crossed a block or retained a same-block duplicate:\n${output}")
endif()

joggle_run("entry specialization failed"
  COMMAND "${TOOL}" run opt.signature
          "${CMAKE_CURRENT_LIST_DIR}/data/opt_signature.jog"
          --arg "\"main\""
          --arg "[\"tensor<f32, [1, 4]>\"]"
          -M "${MODULES}"
  OUTPUT_VARIABLE specialized
  ERROR_VARIABLE error)
if(NOT specialized MATCHES
   "fn main\\(x: tensor<f32, \\[1, 4\\]>\\) -> tensor<f32, \\[1, _\\]>")
  message(FATAL_ERROR
          "entry template or anonymous extent was not specialized:\n${specialized}")
endif()

joggle_run("concrete signature guard failed"
  COMMAND "${TOOL}" run opt.signature
          "${CMAKE_CURRENT_LIST_DIR}/data/opt_signature.jog"
          --arg "\"fixed\""
          --arg "[\"tensor<f32, [3, 4]>\"]"
          -M "${MODULES}"
  OUTPUT_VARIABLE guarded
  ERROR_VARIABLE error)
if(NOT guarded MATCHES "fn fixed\\(x: tensor<f32, \\[2, 4\\]>")
  message(FATAL_ERROR "opt.signature rewrote a concrete extent:\n${guarded}")
endif()

joggle_run("overload-safe optimization failed"
  COMMAND "${TOOL}" run opt.fold_add_zero opt.basic "${MODEL}"
          -M "${MODULES}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
string(FIND "${output}" "return x + i32(0)" used)
string(FIND "${output}" "let observed = x + i32(1)" unused)
if(used EQUAL -1 OR unused EQUAL -1)
  message(FATAL_ERROR
          "built-in algebra was applied to a user overload:\n${output}")
endif()
