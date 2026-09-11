if(NOT DEFINED TEST OR NOT DEFINED CC OR NOT DEFINED CASE OR
   NOT DEFINED MODULES OR NOT DEFINED ROOT)
  message(FATAL_ERROR
          "ONNX execution test requires TEST, CC, CASE, MODULES, and ROOT")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")
set(source "${ROOT}/model.c")
set(harness "${ROOT}/harness.c")
set(program "${ROOT}/model")

execute_process(
  COMMAND "${TEST}"
          "${CASE}/model.onnx"
          "${CASE}/test_data_set_0/input_0.pb"
          "${CASE}/test_data_set_0/input_1.pb"
          "${CASE}/test_data_set_0/output_0.pb"
          "${source}" "${harness}" "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "ONNX VM execution failed (${result}):\n${output}${error}")
endif()

execute_process(
  COMMAND "${CC}" -std=c99 -Wall -Wextra -Werror
          "${source}" "${harness}" -o "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  file(READ "${source}" emitted)
  message(FATAL_ERROR
          "ONNX-generated C did not compile (${result}):\n"
          "${output}${error}\n${emitted}")
endif()

execute_process(
  COMMAND "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "ONNX-generated C disagrees with the official output (${result}):\n"
          "${output}${error}")
endif()

file(REMOVE_RECURSE "${ROOT}")
