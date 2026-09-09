if(NOT DEFINED OUT OR OUT STREQUAL "")
  message(FATAL_ERROR "set OUT to the destination .onnx file")
endif()

set(revision b055c14ebe95ca2df473547484e4d447867951ed)
set(sha256 c1c513582d56afceff8516c73804e484c81c6a830712ab6d682253f4a3cd042f)
set(url "https://huggingface.co/onnxmodelzoo/mobilenetv2-7/resolve/${revision}/mobilenetv2-7.onnx?download=true")

get_filename_component(directory "${OUT}" DIRECTORY)
if(NOT directory STREQUAL "")
  file(MAKE_DIRECTORY "${directory}")
endif()
file(
  DOWNLOAD "${url}" "${OUT}"
  EXPECTED_HASH "SHA256=${sha256}"
  SHOW_PROGRESS
  STATUS status
  TLS_VERIFY ON
)
list(GET status 0 code)
list(GET status 1 message)
if(NOT code EQUAL 0)
  message(FATAL_ERROR "model download failed: ${message}")
endif()
