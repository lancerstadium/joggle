foreach(argument IN ITEMS
    JOGGLE_CLI JOGGLE_ARITH JOGGLE_FIXED JOGGLE_TENSOR JOGGLE_NN JOGGLE_MLP
    JOGGLE_EDGEVEC JOGGLE_ARITH_BEHAVIOR JOGGLE_FIXED_BEHAVIOR
    JOGGLE_TENSOR_BEHAVIOR JOGGLE_EDGEVEC_BEHAVIOR JOGGLE_VERTICAL_TEST_DIR)
  if(NOT DEFINED ${argument})
    message(FATAL_ERROR "vertical CLI test is missing ${argument}")
  endif()
endforeach()

file(REMOVE_RECURSE "${JOGGLE_VERTICAL_TEST_DIR}")
file(MAKE_DIRECTORY "${JOGGLE_VERTICAL_TEST_DIR}")

function(expect_canonical source)
  file(READ "${source}" expected)
  execute_process(
    COMMAND "${JOGGLE_CLI}" fmt "${source}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE formatted
    ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "cannot format ${source}:\n${error}")
  endif()
  if(NOT formatted STREQUAL expected)
    message(FATAL_ERROR "shipped Module is not canonical: ${source}")
  endif()
endfunction()

foreach(source IN ITEMS
    "${JOGGLE_ARITH}" "${JOGGLE_FIXED}" "${JOGGLE_TENSOR}" "${JOGGLE_NN}"
    "${JOGGLE_MLP}" "${JOGGLE_EDGEVEC}")
  expect_canonical("${source}")
endforeach()

function(install_module source)
  set(arguments install "${source}" --root "${JOGGLE_VERTICAL_TEST_DIR}")
  if(ARGC GREATER 1)
    list(APPEND arguments --behavior "${ARGV1}")
  endif()
  execute_process(
    COMMAND "${JOGGLE_CLI}" ${arguments}
    RESULT_VARIABLE result
    ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "cannot install ${source}:\n${error}")
  endif()
endfunction()

install_module("${JOGGLE_ARITH}" "${JOGGLE_ARITH_BEHAVIOR}")
install_module("${JOGGLE_FIXED}" "${JOGGLE_FIXED_BEHAVIOR}")
install_module("${JOGGLE_TENSOR}" "${JOGGLE_TENSOR_BEHAVIOR}")
install_module("${JOGGLE_NN}")
install_module("${JOGGLE_MLP}")
install_module("${JOGGLE_EDGEVEC}" "${JOGGLE_EDGEVEC_BEHAVIOR}")

execute_process(
  COMMAND "${JOGGLE_CLI}" check "${JOGGLE_MLP}"
          --root "${JOGGLE_VERTICAL_TEST_DIR}"
  RESULT_VARIABLE check_result
  ERROR_VARIABLE check_error
)
if(NOT check_result EQUAL 0)
  message(FATAL_ERROR "installed model closure does not check:\n${check_error}")
endif()

execute_process(
  COMMAND "${JOGGLE_CLI}" lock "${JOGGLE_MLP}"
          --root "${JOGGLE_VERTICAL_TEST_DIR}"
          -o "${JOGGLE_VERTICAL_TEST_DIR}/mlp.lock"
  RESULT_VARIABLE lock_result
  ERROR_VARIABLE lock_error
)
if(NOT lock_result EQUAL 0)
  message(FATAL_ERROR "installed model closure does not lock:\n${lock_error}")
endif()

function(lower_graph graph expected_format output)
  execute_process(
    COMMAND "${JOGGLE_CLI}" run "${JOGGLE_MLP}" "${graph}"
            edgevec.lower --with "${JOGGLE_EDGEVEC}"
            --root "${JOGGLE_VERTICAL_TEST_DIR}"
            --load-behavior "arith=${JOGGLE_ARITH_BEHAVIOR}"
            --load-behavior "fixed=${JOGGLE_FIXED_BEHAVIOR}"
            --load-behavior "tensor=${JOGGLE_TENSOR_BEHAVIOR}"
            --load-behavior "edgevec=${JOGGLE_EDGEVEC_BEHAVIOR}"
            -o "${output}"
    RESULT_VARIABLE result
    ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "cannot lower mlp.${graph}:\n${error}")
  endif()

  file(READ "${output}" lowered)
  string(FIND "${lowered}"
    "module mlp_${graph}_compiled@1.0.0" module_position)
  string(FIND "${lowered}" "graph ${graph}(" graph_position)
  string(FIND "${lowered}" "tensor.constant(" constant_position)
  string(FIND "${lowered}" "tensor.reshape(" reshape_position)
  string(FIND "${lowered}" "nn.linear(" linear_position)
  string(FIND "${lowered}" "nn.relu(" relu_position)
  string(FIND "${lowered}" "edgevec.dot(" dot_position)
  string(FIND "${lowered}" "edgevec.clamp(" clamp_position)
  string(FIND "${lowered}" "${expected_format}" format_position)
  if(module_position EQUAL -1 OR graph_position EQUAL -1 OR
     constant_position EQUAL -1 OR reshape_position EQUAL -1 OR
     dot_position EQUAL -1 OR
     clamp_position EQUAL -1 OR format_position EQUAL -1 OR
     NOT linear_position EQUAL -1 OR NOT relu_position EQUAL -1)
    message(FATAL_ERROR
      "lowered mlp.${graph} is not one ordinary graph:\n${lowered}")
  endif()

  execute_process(
    COMMAND "${JOGGLE_CLI}" check "${output}"
            --root "${JOGGLE_VERTICAL_TEST_DIR}"
    RESULT_VARIABLE check_output_result
    ERROR_VARIABLE check_output_error
  )
  if(NOT check_output_result EQUAL 0)
    message(FATAL_ERROR
      "lowered mlp.${graph} cannot be loaded again:\n${check_output_error}")
  endif()

  execute_process(
    COMMAND "${JOGGLE_CLI}" install "${output}"
            --root "${JOGGLE_VERTICAL_TEST_DIR}"
    RESULT_VARIABLE install_output_result
    ERROR_VARIABLE install_output_error
  )
  if(NOT install_output_result EQUAL 0)
    message(FATAL_ERROR
      "lowered mlp.${graph} cannot coexist with its source Module:\n"
      "${install_output_error}")
  endif()

  set(replayed "${output}.replayed")
  execute_process(
    COMMAND "${JOGGLE_CLI}" run "${output}" "${graph}"
            --root "${JOGGLE_VERTICAL_TEST_DIR}" -o "${replayed}"
    RESULT_VARIABLE replay_result
    ERROR_VARIABLE replay_error
  )
  if(NOT replay_result EQUAL 0)
    message(FATAL_ERROR
      "lowered mlp.${graph} cannot run again:\n${replay_error}")
  endif()
  file(READ "${replayed}" replayed_text)
  if(NOT replayed_text STREQUAL lowered)
    message(FATAL_ERROR
      "replaying mlp.${graph} changed its derived Module identity:\n"
      "${replayed_text}")
  endif()
endfunction()

lower_graph(main "arith.integer<8>"
  "${JOGGLE_VERTICAL_TEST_DIR}/integer.joggle")
lower_graph(fixed "fixed.q<8, 4>"
  "${JOGGLE_VERTICAL_TEST_DIR}/fixed.joggle")
