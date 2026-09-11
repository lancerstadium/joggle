if(NOT DEFINED TEST OR NOT DEFINED CC OR NOT DEFINED CASE OR
   NOT DEFINED MODULES OR NOT DEFINED HARNESS OR NOT DEFINED ROOT)
  message(FATAL_ERROR
          "ONNX application test requires TEST, CC, CASE, MODULES, HARNESS, and ROOT")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")
set(source "${ROOT}/model.c")
set(input "${ROOT}/input.bin")
set(expected "${ROOT}/expected.bin")
set(program "${ROOT}/model")
set(prepared "${ROOT}/model.jog")
set(image "${ROOT}/model.vm")

execute_process(
  COMMAND "${TEST}"
          "${CASE}/mobilenetv2-7.onnx"
          "${CASE}/test_data_set_0/input_0.pb"
          "${CASE}/test_data_set_0/output_0.pb"
          "${source}" "${input}" "${expected}" "${MODULES}"
          "${prepared}" "${image}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "MobileNet preparation or VM execution failed (${result}):\n${output}${error}")
endif()
set(vm_output "${output}")

execute_process(
  COMMAND "${CC}" -std=c99 -O1 -Wall -Wextra -Werror
          "${source}" "${HARNESS}" -lm -o "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "MobileNet-generated C did not compile (${result}):\n${output}${error}")
endif()

execute_process(
  COMMAND "${program}" "${input}" "${expected}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "MobileNet output disagrees with ONNX Zoo (${result}):\n${output}${error}")
endif()
file(WRITE "${ROOT}/result.txt" "${vm_output}${output}")
message(STATUS "${vm_output}${output}")
