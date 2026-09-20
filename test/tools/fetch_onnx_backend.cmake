if(NOT DEFINED OUT OR OUT STREQUAL "")
  message(FATAL_ERROR "set OUT to the ONNX backend-case directory")
endif()

set(case test_matmul_2d)
set(files
  model.onnx
  test_data_set_0/input_0.pb
  test_data_set_0/input_1.pb
  test_data_set_0/output_0.pb
)
set(hashes
  946f3a49ccfdaf62586bbae8438065fe41a7e6a9b8006b14af3219999b49bb76
  5c9fa13b29d31b4105ea162c0caecc951804499b016bdf268b8f470b404c1f79
  7d950d6d6af29dfa3acab8c9fcdea76d553c11124013b106eb6622bc0ebf3dd4
  c7928ba3e8fe85076974c955200532c2a24697119eb54b338fbc7c2639228e48
)
set(root
  "https://raw.githubusercontent.com/onnx/onnx/v1.19.0/onnx/backend/test/data/node/${case}"
)

list(LENGTH files count)
math(EXPR last "${count} - 1")
foreach(index RANGE ${last})
  list(GET files ${index} file)
  list(GET hashes ${index} hash)
  get_filename_component(directory "${OUT}/${case}/${file}" DIRECTORY)
  file(MAKE_DIRECTORY "${directory}")
  message(STATUS "Downloading ${case}/${file}")
  file(
    DOWNLOAD "${root}/${file}" "${OUT}/${case}/${file}"
    EXPECTED_HASH "SHA256=${hash}"
    SHOW_PROGRESS
    STATUS status
    TLS_VERIFY ON
  )
  list(GET status 0 code)
  list(GET status 1 message)
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "${case}/${file} download failed: ${message}")
  endif()
endforeach()
