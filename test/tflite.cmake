if(NOT DEFINED OUT OR OUT STREQUAL "")
  message(FATAL_ERROR "set OUT to the destination .tflite file")
endif()

set(hash "ff5cb7f9e62c92ebdad971f8a98aa6b3106d82a64587a7787c6a385c9e791339")

if(EXISTS "${OUT}")
  file(SHA256 "${OUT}" actual)
endif()
if(NOT actual STREQUAL hash)
  get_filename_component(directory "${OUT}" DIRECTORY)
  if(NOT directory STREQUAL "")
    file(MAKE_DIRECTORY "${directory}")
  endif()
  file(DOWNLOAD
    "https://tfhub.dev/tensorflow/lite-model/mobilenet_v2_1.0_224/1/metadata/1?lite-format=tflite"
    "${OUT}"
    EXPECTED_HASH "SHA256=${hash}"
    SHOW_PROGRESS
    STATUS status
  )
  list(GET status 0 code)
  if(NOT code EQUAL 0)
    list(GET status 1 message)
    file(REMOVE "${OUT}")
    message(FATAL_ERROR "TFLite model download failed: ${message}")
  endif()
endif()

message(STATUS "Pinned TFLite model: ${OUT}")
