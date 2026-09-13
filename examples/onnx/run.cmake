if(NOT DEFINED APP OR NOT DEFINED TOOL OR NOT DEFINED CC OR NOT DEFINED MODEL OR
   NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED MODULES OR
   NOT DEFINED PYTHON OR NOT DEFINED GENERATOR OR NOT DEFINED ROOT OR
   NOT DEFINED VM)
  message(FATAL_ERROR
          "ONNX example requires APP, TOOL, CC, MODEL, INPUT, OUTPUT, MODULES, "
          "PYTHON, GENERATOR, ROOT, and VM")
endif()

set(vm_mode no-vm)
if(VM)
  set(vm_mode vm)
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
set(blob_header "${ROOT}/model-blob.h")
set(blob_program "${ROOT}/model-blob")
set(api "${ROOT}/api.json")
set(blob_api "${ROOT}/api-blob.json")
set(harness "${ROOT}/harness.c")
set(blob_harness "${ROOT}/harness-blob.c")

execute_process(
  COMMAND "${APP}" "${MODEL}" "${INPUT}" "${OUTPUT}"
          "${source}" "${input}" "${expected}" "${MODULES}"
          "${prepared}" "${image}" "${header}" "${vm_mode}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "ONNX application preparation failed (${result}):\n${output}${error}")
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
  COMMAND "${TOOL}" query c.api "${prepared}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${api}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "ONNX C API query failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${PYTHON}" "${GENERATOR}" "${api}" "${harness}"
          --header model.h
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "ONNX harness generation failed (${result}):\n${output}${error}")
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
  COMMAND "${TOOL}" emit c.header "${prepared}"
          --arg "\"weights\"" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${blob_header}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "ONNX external-data header emission failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" query c.api "${prepared}"
          --arg "\"weights\"" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${blob_api}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "ONNX external-data C API query failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${PYTHON}" "${GENERATOR}" "${blob_api}" "${blob_harness}"
          --header model-blob.h
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "external-data harness generation failed (${result}):\n"
          "${output}${error}")
endif()

execute_process(
  COMMAND "${CC}" -std=c11 -O3 -Wall -Wextra -Wstrict-prototypes -Werror
          -I "${ROOT}" "${blob_source}" "${blob_harness}" -lm
          -o "${blob_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "external-data ONNX C did not compile (${result}):\n${output}${error}")
endif()

execute_process(
  COMMAND "${CC}" -std=c11 -O3 -Wall -Wextra -Wstrict-prototypes -Werror
          -I "${ROOT}" "${source}" "${harness}" -lm -o "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "generated ONNX C did not compile (${result}):\n${output}${error}")
endif()

execute_process(
  COMMAND "${program}" "${input}" "${expected}" 0 1
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "generated ONNX C disagrees with the reference (${result}):\n"
          "${output}${error}")
endif()
set(inline_output "${error}${output}")

execute_process(
  COMMAND "${blob_program}" "${input}" "${data}" "${expected}" 0 1
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "external-data ONNX C disagrees with the reference (${result}):\n"
          "${output}${error}")
endif()
file(WRITE "${ROOT}/result.txt"
     "${vm_output}inline: ${inline_output}external: ${error}${output}")
message(STATUS
        "${vm_output}inline: ${inline_output}external: ${error}${output}")
