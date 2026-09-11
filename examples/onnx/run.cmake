if(NOT DEFINED APP OR NOT DEFINED CC OR NOT DEFINED MODEL OR
   NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED MODULES OR
   NOT DEFINED HARNESS OR NOT DEFINED ROOT)
  message(FATAL_ERROR
          "ONNX example requires APP, CC, MODEL, INPUT, OUTPUT, MODULES, "
          "HARNESS, and ROOT")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")
set(source "${ROOT}/model.c")
set(header "${ROOT}/model.h")
set(input "${ROOT}/input.bin")
set(expected "${ROOT}/expected.bin")
set(program "${ROOT}/model")
set(prepared "${ROOT}/model.jog")
set(image "${ROOT}/model.vm")

execute_process(
  COMMAND "${APP}" "${MODEL}" "${INPUT}" "${OUTPUT}"
          "${source}" "${input}" "${expected}" "${MODULES}"
          "${prepared}" "${image}" "${header}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "ONNX preparation or VM execution failed (${result}):\n${output}${error}")
endif()
set(vm_output "${output}")

execute_process(
  COMMAND "${CC}" -std=c99 -O1 -Wall -Wextra -Wstrict-prototypes -Werror
          -I "${ROOT}" "${source}" "${HARNESS}" -lm -o "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "generated ONNX C did not compile (${result}):\n${output}${error}")
endif()

execute_process(
  COMMAND "${program}" "${input}" "${expected}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "generated ONNX C disagrees with the reference (${result}):\n"
          "${output}${error}")
endif()
file(WRITE "${ROOT}/result.txt" "${vm_output}${output}")
message(STATUS "${vm_output}${output}")
