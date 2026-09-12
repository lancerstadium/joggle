if(NOT DEFINED APP OR NOT DEFINED TOOL OR NOT DEFINED CC OR NOT DEFINED MODEL OR
   NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED MODULES OR
   NOT DEFINED HARNESS OR NOT DEFINED ROOT)
  message(FATAL_ERROR
          "ONNX example requires APP, TOOL, CC, MODEL, INPUT, OUTPUT, MODULES, "
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
set(bounds "${ROOT}/bounds.json")
set(data "${ROOT}/model.bin")
set(blob_source "${ROOT}/model-blob.c")
set(blob_object "${ROOT}/model-blob.o")

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
  COMMAND "${TOOL}" query bounds.report "${prepared}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${bounds}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "ONNX bounds analysis failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.data "${prepared}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${data}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "ONNX data emission failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.source "${prepared}"
          --arg "\"weights\"" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${blob_source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "ONNX external-data C emission failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${CC}" -std=c99 -O1 -Wall -Wextra -Wstrict-prototypes -Werror
          -I "${ROOT}" -c "${blob_source}" -o "${blob_object}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "external-data ONNX C did not compile (${result}):\n${output}${error}")
endif()

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
