foreach(required JOGGLE_CLI JOGGLE_ARITH_MOD JOGGLE_TENSOR_MOD JOGGLE_ONNX_MOD
                 JOGGLE_ONNX_NATIVE JOGGLE_ONNX_MODEL JOGGLE_BUNDLE_TEST_DIR)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} was not provided")
  endif()
endforeach()

file(REMOVE_RECURSE "${JOGGLE_BUNDLE_TEST_DIR}")
file(MAKE_DIRECTORY "${JOGGLE_BUNDLE_TEST_DIR}")
set(driver "${JOGGLE_BUNDLE_TEST_DIR}/driver.joggle")
if(NOT DEFINED JOGGLE_MODEL_NAME)
  set(JOGGLE_MODEL_NAME squeezenet)
endif()
set(bundle "${JOGGLE_BUNDLE_TEST_DIR}/${JOGGLE_MODEL_NAME}")
set(root "${JOGGLE_BUNDLE_TEST_DIR}/mods")
set(lock "${JOGGLE_BUNDLE_TEST_DIR}/joggle.lock")
set(optional_import "")
if(DEFINED JOGGLE_QUANT_MOD)
  set(optional_import "  import quant@2;\n")
endif()
file(WRITE "${driver}" "joggle 1;

mod onnx_bundle_test@1.0.0 {
  import onnx@2;
${optional_import}
  fn read(input: bytes) -> mod {
    return @onnx.read(input, \"${JOGGLE_MODEL_NAME}\");
  }
}
")

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

set(with_mods --with "${JOGGLE_ARITH_MOD}" --with "${JOGGLE_TENSOR_MOD}")
if(DEFINED JOGGLE_QUANT_MOD)
  list(APPEND with_mods --with "${JOGGLE_QUANT_MOD}")
endif()
list(APPEND with_mods --with "${JOGGLE_ONNX_MOD}")
expect_success("import reference model"
  "${JOGGLE_CLI}" run "${driver}" read "${JOGGLE_ONNX_MODEL}"
  ${with_mods}
  --load-native "onnx=${JOGGLE_ONNX_NATIVE}"
  -o "${bundle}")

if(NOT EXISTS "${bundle}/mod.joggle")
  message(FATAL_ERROR "ONNX import did not produce a Mod bundle")
endif()
file(GLOB payloads "${bundle}/data/*")
list(LENGTH payloads payload_count)
if(DEFINED JOGGLE_EXPECTED_PAYLOADS AND
   NOT payload_count EQUAL JOGGLE_EXPECTED_PAYLOADS)
  message(FATAL_ERROR
    "ONNX bundle has ${payload_count} payloads instead of "
    "${JOGGLE_EXPECTED_PAYLOADS}")
endif()

expect_success("install arithmetic dependency"
  "${JOGGLE_CLI}" install "${JOGGLE_ARITH_MOD}" --root "${root}")
expect_success("install tensor dependency"
  "${JOGGLE_CLI}" install "${JOGGLE_TENSOR_MOD}" --root "${root}")
if(DEFINED JOGGLE_QUANT_MOD)
  expect_success("install quant dependency"
    "${JOGGLE_CLI}" install "${JOGGLE_QUANT_MOD}" --root "${root}")
endif()
expect_success("install ONNX dependency"
  "${JOGGLE_CLI}" install "${JOGGLE_ONNX_MOD}" --root "${root}")
expect_success("check imported bundle"
  "${JOGGLE_CLI}" check "${bundle}" --root "${root}")
expect_success("install imported bundle"
  "${JOGGLE_CLI}" install "${bundle}" --root "${root}")
string(STRIP "${command_output}" installed_source)
get_filename_component(installed_identity "${installed_source}" DIRECTORY)
file(GLOB installed_payloads "${installed_identity}/data/*")
list(LENGTH installed_payloads installed_payload_count)
if(NOT installed_payload_count EQUAL payload_count)
  message(FATAL_ERROR "installed ONNX Mod lost payloads")
endif()

expect_success("check installed identity bundle"
  "${JOGGLE_CLI}" check "${installed_identity}" --root "${root}")
expect_success("lock installed identity bundle"
  "${JOGGLE_CLI}" lock "${installed_identity}" --root "${root}"
  -o "${lock}")
file(READ "${lock}" lock_text)
string(FIND "${lock_text}" "root ${JOGGLE_MODEL_NAME}@1.0.0#" root_position)
string(FIND "${lock_text}" "mod arith@1.0.0#" arith_position)
string(FIND "${lock_text}" "mod tensor@2.0.0#" tensor_position)
string(FIND "${lock_text}" "mod onnx@2.0.0#" onnx_position)
set(quant_position 0)
if(DEFINED JOGGLE_QUANT_MOD)
  string(FIND "${lock_text}" "mod quant@2.0.0#" quant_position)
endif()
if(root_position EQUAL -1 OR arith_position EQUAL -1 OR
   tensor_position EQUAL -1 OR onnx_position EQUAL -1 OR
   quant_position EQUAL -1)
  message(FATAL_ERROR "installed ONNX Mod lock is incomplete")
endif()
