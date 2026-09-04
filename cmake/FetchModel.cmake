foreach(required IN ITEMS JOGGLE_MODEL_OUTPUT JOGGLE_MODEL_URL
                          JOGGLE_MODEL_SHA256 JOGGLE_MODEL_NAME)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

get_filename_component(model_directory "${JOGGLE_MODEL_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${model_directory}")

if(EXISTS "${JOGGLE_MODEL_OUTPUT}")
  file(SHA256 "${JOGGLE_MODEL_OUTPUT}" cached_sha256)
  if(cached_sha256 STREQUAL JOGGLE_MODEL_SHA256)
    message(STATUS "${JOGGLE_MODEL_NAME} already present and verified")
    return()
  endif()
  message(FATAL_ERROR
    "cached ${JOGGLE_MODEL_NAME} has SHA-256 ${cached_sha256}; expected "
    "${JOGGLE_MODEL_SHA256}. Remove only this cache file and retry: "
    "${JOGGLE_MODEL_OUTPUT}")
endif()

set(partial "${JOGGLE_MODEL_OUTPUT}.part")
file(REMOVE "${partial}")
file(DOWNLOAD "${JOGGLE_MODEL_URL}" "${partial}"
  EXPECTED_HASH "SHA256=${JOGGLE_MODEL_SHA256}"
  STATUS download_status
  SHOW_PROGRESS
  TLS_VERIFY ON)
list(GET download_status 0 download_code)
list(GET download_status 1 download_message)
if(NOT download_code EQUAL 0)
  file(REMOVE "${partial}")
  message(FATAL_ERROR
    "${JOGGLE_MODEL_NAME} download failed: ${download_message}")
endif()
file(RENAME "${partial}" "${JOGGLE_MODEL_OUTPUT}")
message(STATUS "Downloaded and verified ${JOGGLE_MODEL_OUTPUT}")
