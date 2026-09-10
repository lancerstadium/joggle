if(NOT DEFINED OUT OR OUT STREQUAL "")
  message(FATAL_ERROR "set OUT to the model directory")
endif()

set(known
  mobilenetv2-7
  squeezenet1.1-7
  squeezenet1.0-13-qdq
  resnet18-v1-7
  tinyyolov2-8
  tiny-yolov3-11
  ultraface-rfb-320
)
if(NOT DEFINED MODELS OR MODELS STREQUAL "")
  set(MODELS ${known})
endif()
foreach(model IN LISTS MODELS)
  if(NOT model IN_LIST known)
    message(FATAL_ERROR "unknown model: ${model}")
  endif()
endforeach()

file(MAKE_DIRECTORY "${OUT}")

function(fetch name revision sha256)
  if(NOT name IN_LIST MODELS)
    return()
  endif()
  set(url
    "https://huggingface.co/onnxmodelzoo/${name}/resolve/${revision}/${name}.onnx?download=true"
  )
  set(output "${OUT}/${name}.onnx")
  message(STATUS "Downloading ${name}")
  file(
    DOWNLOAD "${url}" "${output}"
    EXPECTED_HASH "SHA256=${sha256}"
    SHOW_PROGRESS
    STATUS status
    TLS_VERIFY ON
  )
  list(GET status 0 code)
  list(GET status 1 message)
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "${name} download failed: ${message}")
  endif()
endfunction()

function(fetch_github name revision source sha256)
  if(NOT name IN_LIST MODELS)
    return()
  endif()
  set(url "https://media.githubusercontent.com/media/onnx/models/${revision}/${source}")
  set(output "${OUT}/${name}.onnx")
  message(STATUS "Downloading ${name}")
  file(
    DOWNLOAD "${url}" "${output}"
    EXPECTED_HASH "SHA256=${sha256}"
    SHOW_PROGRESS
    STATUS status
    TLS_VERIFY ON
  )
  list(GET status 0 code)
  list(GET status 1 message)
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "${name} download failed: ${message}")
  endif()
endfunction()

fetch(
  mobilenetv2-7
  b055c14ebe95ca2df473547484e4d447867951ed
  c1c513582d56afceff8516c73804e484c81c6a830712ab6d682253f4a3cd042f
)
fetch(
  squeezenet1.1-7
  61e525224ad479521059f4586bcacf50ad3627ca
  1eeff551a67ae8d565ca33b572fc4b66e3ef357b0eb2863bb9ff47a918cc4088
)
fetch(
  squeezenet1.0-13-qdq
  dce102eb665be44d95321bf86fd9244014755195
  4a567dd7542ef440890d57268fabf47211174c593d7a1837bd7f16a1067169e7
)
fetch(
  resnet18-v1-7
  e7cb849a949bdceba02356b8b923d53cc01108e1
  4e8f8653e7a2222b3904cc3fe8e304cd8b339ce1d05fd24688162f86fb6df52c
)
fetch(
  tinyyolov2-8
  869707e16e57006f97d98af54cfdc8a1d388ae61
  583fb7fdc948435ceac9fa82efc7708701efe8382a859a3dd46526b155f5f2ae
)
fetch_github(
  tiny-yolov3-11
  4c46cd00fbdb7cd30b6c1c17ab54f2e1f4f7b177
  validated/vision/object_detection_segmentation/tiny-yolov3/model/tiny-yolov3-11.onnx
  f715cc2d99740d22d312777e20d9de2b2ecdc250155be8fd3752ce7e8b823521
)
fetch_github(
  ultraface-rfb-320
  4c46cd00fbdb7cd30b6c1c17ab54f2e1f4f7b177
  validated/vision/body_analysis/ultraface/models/version-RFB-320.onnx
  34cd7e60aeff28744c657de7a3dc64e872d506741de66987f3426f2b79f88017
)
