foreach(required JOGGLE_CLI JOGGLE_TENSOR_MODULE JOGGLE_ONNX_MODULE
                 JOGGLE_ONNX_NATIVE JOGGLE_ONNX_MODEL JOGGLE_BUNDLE_TEST_DIR)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} was not provided")
  endif()
endforeach()

file(REMOVE_RECURSE "${JOGGLE_BUNDLE_TEST_DIR}")
file(MAKE_DIRECTORY "${JOGGLE_BUNDLE_TEST_DIR}")
set(driver "${JOGGLE_BUNDLE_TEST_DIR}/driver.joggle")
set(bundle "${JOGGLE_BUNDLE_TEST_DIR}/squeezenet")
set(root "${JOGGLE_BUNDLE_TEST_DIR}/modules")
set(lock "${JOGGLE_BUNDLE_TEST_DIR}/joggle.lock")
file(WRITE "${driver}" [=[joggle 1;

module onnx_bundle_test@1.0.0 {
  import onnx@1;

  fn read(input: bytes) -> module {
    return @onnx.read(input, "squeezenet");
  }
}
]=])

function(expect_success label)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${label} failed:\n${output}${error}")
  endif()
  set(command_output "${output}" PARENT_SCOPE)
endfunction()

expect_success("import reference model"
  "${JOGGLE_CLI}" run "${driver}" read "${JOGGLE_ONNX_MODEL}"
  --with "${JOGGLE_TENSOR_MODULE}"
  --with "${JOGGLE_ONNX_MODULE}"
  --load-native "onnx=${JOGGLE_ONNX_NATIVE}"
  -o "${bundle}")

if(NOT EXISTS "${bundle}/module.joggle")
  message(FATAL_ERROR "ONNX import did not produce a Module bundle")
endif()
file(GLOB payloads "${bundle}/data/*")
list(LENGTH payloads payload_count)
if(NOT payload_count EQUAL 54)
  message(FATAL_ERROR
    "ONNX bundle has ${payload_count} payloads instead of 54")
endif()

expect_success("install tensor dependency"
  "${JOGGLE_CLI}" install "${JOGGLE_TENSOR_MODULE}" --root "${root}")
expect_success("check imported bundle"
  "${JOGGLE_CLI}" check "${bundle}" --root "${root}")
expect_success("install imported bundle"
  "${JOGGLE_CLI}" install "${bundle}" --root "${root}")
string(STRIP "${command_output}" installed_source)
get_filename_component(installed_identity "${installed_source}" DIRECTORY)
file(GLOB installed_payloads "${installed_identity}/data/*")
list(LENGTH installed_payloads installed_payload_count)
if(NOT installed_payload_count EQUAL 54)
  message(FATAL_ERROR "installed ONNX Module lost payloads")
endif()

expect_success("check installed identity bundle"
  "${JOGGLE_CLI}" check "${installed_identity}" --root "${root}")
expect_success("lock installed identity bundle"
  "${JOGGLE_CLI}" lock "${installed_identity}" --root "${root}"
  -o "${lock}")
file(READ "${lock}" lock_text)
string(FIND "${lock_text}" "root squeezenet@1.0.0#" root_position)
string(FIND "${lock_text}" "module tensor@1.0.0#" tensor_position)
if(root_position EQUAL -1 OR tensor_position EQUAL -1)
  message(FATAL_ERROR "installed ONNX Module lock is incomplete")
endif()
