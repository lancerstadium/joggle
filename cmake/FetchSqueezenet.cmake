if(NOT DEFINED JOGGLE_MODEL_OUTPUT OR JOGGLE_MODEL_OUTPUT STREQUAL "")
  message(FATAL_ERROR "JOGGLE_MODEL_OUTPUT must name the model cache file")
endif()

set(model_url
  "https://media.githubusercontent.com/media/onnx/models/4c46cd00fbdb7cd30b6c1c17ab54f2e1f4f7b177/validated/vision/classification/squeezenet/model/squeezenet1.1-7.onnx")
set(model_sha256
  "1eeff551a67ae8d565ca33b572fc4b66e3ef357b0eb2863bb9ff47a918cc4088")

get_filename_component(model_directory "${JOGGLE_MODEL_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${model_directory}")

if(EXISTS "${JOGGLE_MODEL_OUTPUT}")
  file(SHA256 "${JOGGLE_MODEL_OUTPUT}" cached_sha256)
  if(cached_sha256 STREQUAL model_sha256)
    message(STATUS "SqueezeNet model already present and verified")
    return()
  endif()
  message(FATAL_ERROR
    "cached SqueezeNet model has SHA-256 ${cached_sha256}; expected "
    "${model_sha256}. Remove only this cache file and retry: "
    "${JOGGLE_MODEL_OUTPUT}")
endif()

set(partial "${JOGGLE_MODEL_OUTPUT}.part")
file(REMOVE "${partial}")
file(DOWNLOAD "${model_url}" "${partial}"
  EXPECTED_HASH "SHA256=${model_sha256}"
  STATUS download_status
  SHOW_PROGRESS
  TLS_VERIFY ON)
list(GET download_status 0 download_code)
list(GET download_status 1 download_message)
if(NOT download_code EQUAL 0)
  file(REMOVE "${partial}")
  message(FATAL_ERROR "SqueezeNet download failed: ${download_message}")
endif()
file(RENAME "${partial}" "${JOGGLE_MODEL_OUTPUT}")
message(STATUS "Downloaded and verified ${JOGGLE_MODEL_OUTPUT}")
