include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

joggle_require("ONNX execution test requires TEST, CC, CASE, MODULES, EXTENSIONS, and ROOT" VARS TEST CC CASE MODULES EXTENSIONS ROOT)

joggle_workspace("${ROOT}")
set(source "${ROOT}/model.c")
set(harness "${ROOT}/harness.c")
set(program "${ROOT}/model")

joggle_run("ONNX VM execution failed"
  COMMAND "${TEST}"
          "${CASE}/model.onnx"
          "${CASE}/test_data_set_0/input_0.pb"
          "${CASE}/test_data_set_0/input_1.pb"
          "${CASE}/test_data_set_0/output_0.pb"
          "${source}" "${harness}" "${MODULES}" "${EXTENSIONS}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

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

joggle_run("ONNX-generated C disagrees with the official output"
  COMMAND "${program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
