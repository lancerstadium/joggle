if(NOT DEFINED OUT OR OUT STREQUAL "")
  message(FATAL_ERROR "set OUT to the model directory")
endif()

set(known
  mnist-8
  mobilenetv2-7
  squeezenet1.1-7
  squeezenet1.0-13-qdq
  resnet18-v1-7
  tinyyolov2-8
  tiny-yolov3-11
  ultraface-rfb-320
  ssd-mobilenetv1-12
  shufflenet-v2-12
  densenet-12
  googlenet-12
  efficientnet-lite4-11-int8
  efficientnet-lite4-11-qdq
  bidaf-9
)
if(NOT DEFINED MODELS OR MODELS STREQUAL "")
  set(MODELS ${known})
  list(REMOVE_ITEM MODELS bidaf-9)
endif()
foreach(model IN LISTS MODELS)
  if(NOT model IN_LIST known)
    message(FATAL_ERROR "unknown model: ${model}")
  endif()
endforeach()

file(MAKE_DIRECTORY "${OUT}")

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

fetch_github(
  mnist-8
  4f43949841cb55a0b98dc8fcd045431ccafd9f96
  validated/vision/classification/mnist/model/mnist-8.onnx
  2f06e72de813a8635c9bc0397ac447a601bdbfa7df4bebc278723b958831c9bf
)
fetch_github(
  mobilenetv2-7
  4f43949841cb55a0b98dc8fcd045431ccafd9f96
  validated/vision/classification/mobilenet/model/mobilenetv2-7.onnx
  c1c513582d56afceff8516c73804e484c81c6a830712ab6d682253f4a3cd042f
)
fetch_github(
  squeezenet1.1-7
  4f43949841cb55a0b98dc8fcd045431ccafd9f96
  validated/vision/classification/squeezenet/model/squeezenet1.1-7.onnx
  1eeff551a67ae8d565ca33b572fc4b66e3ef357b0eb2863bb9ff47a918cc4088
)
fetch_github(
  squeezenet1.0-13-qdq
  4f43949841cb55a0b98dc8fcd045431ccafd9f96
  validated/vision/classification/squeezenet/model/squeezenet1.0-13-qdq.onnx
  4a567dd7542ef440890d57268fabf47211174c593d7a1837bd7f16a1067169e7
)
fetch_github(
  resnet18-v1-7
  4f43949841cb55a0b98dc8fcd045431ccafd9f96
  validated/vision/classification/resnet/model/resnet18-v1-7.onnx
  4e8f8653e7a2222b3904cc3fe8e304cd8b339ce1d05fd24688162f86fb6df52c
)
fetch_github(
  tinyyolov2-8
  4f43949841cb55a0b98dc8fcd045431ccafd9f96
  validated/vision/object_detection_segmentation/tiny-yolov2/model/tinyyolov2-8.onnx
  583fb7fdc948435ceac9fa82efc7708701efe8382a859a3dd46526b155f5f2ae
)
fetch_github(
  tiny-yolov3-11
  4f43949841cb55a0b98dc8fcd045431ccafd9f96
  validated/vision/object_detection_segmentation/tiny-yolov3/model/tiny-yolov3-11.onnx
  f715cc2d99740d22d312777e20d9de2b2ecdc250155be8fd3752ce7e8b823521
)
fetch_github(
  ultraface-rfb-320
  4f43949841cb55a0b98dc8fcd045431ccafd9f96
  validated/vision/body_analysis/ultraface/models/version-RFB-320.onnx
  34cd7e60aeff28744c657de7a3dc64e872d506741de66987f3426f2b79f88017
)
fetch_github(
  ssd-mobilenetv1-12
  4f43949841cb55a0b98dc8fcd045431ccafd9f96
  validated/vision/object_detection_segmentation/ssd-mobilenetv1/model/ssd_mobilenet_v1_12.onnx
  b8fba5e404077d4048d27fcd1667e85e27e192eb9bf51e696c46a3acd7d21058
)
fetch_github(
  shufflenet-v2-12
  4f43949841cb55a0b98dc8fcd045431ccafd9f96
  validated/vision/classification/shufflenet/model/shufflenet-v2-12.onnx
  ea69821b4dd374ae2f33f9710dd1229ac263d0ee5b5a46ca3521f6483e1ba035
)
fetch_github(
  densenet-12
  4f43949841cb55a0b98dc8fcd045431ccafd9f96
  validated/vision/classification/densenet-121/model/densenet-12.onnx
  0294e7e88e5b3360de9b0fdc321baf9e6ef18b7f058c4536caae3b9f18ed9ed5
)
fetch_github(
  googlenet-12
  4f43949841cb55a0b98dc8fcd045431ccafd9f96
  validated/vision/classification/inception_and_googlenet/googlenet/model/googlenet-12.onnx
  c99c507058eaf41de8723408fdda7db8325cb57f0a89f2ee07a716d6e963e14e
)
fetch_github(
  efficientnet-lite4-11-int8
  4f43949841cb55a0b98dc8fcd045431ccafd9f96
  validated/vision/classification/efficientnet-lite4/model/efficientnet-lite4-11-int8.onnx
  2b3cbb5077262b20df565dacddecb3724c0976c35029a87e512d13aa4eff04a2
)
fetch_github(
  efficientnet-lite4-11-qdq
  4f43949841cb55a0b98dc8fcd045431ccafd9f96
  validated/vision/classification/efficientnet-lite4/model/efficientnet-lite4-11-qdq.onnx
  6837d0b19625d4aff8266d7197a7f3775afd82a8c40f9fd0283d52db4955566f
)
fetch_github(
  bidaf-9
  4f43949841cb55a0b98dc8fcd045431ccafd9f96
  validated/text/machine_comprehension/bidirectional_attention_flow/model/bidaf-9.onnx
  dfc317b56d065a3e297240a9e9b9118ff2260790b5850f4be2bc6ea1bcc65e80
)

function(fetch_app name source top sha256)
  if(NOT name IN_LIST MODELS)
    return()
  endif()
  set(archive "${OUT}/${name}.tar.gz")
  set(stage "${OUT}/.app-${name}")
  file(
    DOWNLOAD
      "https://media.githubusercontent.com/media/onnx/models/4f43949841cb55a0b98dc8fcd045431ccafd9f96/${source}"
      "${archive}"
    EXPECTED_HASH
      "SHA256=${sha256}"
    SHOW_PROGRESS
    STATUS status
    TLS_VERIFY ON
  )
  list(GET status 0 code)
  list(GET status 1 message)
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "${name} application download failed: ${message}")
  endif()
  file(REMOVE_RECURSE "${stage}" "${OUT}/app/${name}")
  file(MAKE_DIRECTORY "${stage}" "${OUT}/app")
  file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${stage}")
  if(NOT IS_DIRECTORY "${stage}/${top}")
    message(FATAL_ERROR "${name} archive has no ${top} directory")
  endif()
  file(RENAME "${stage}/${top}" "${OUT}/app/${name}")
  file(REMOVE_RECURSE "${stage}")
  file(REMOVE "${archive}")
endfunction()

if(DEFINED APP AND APP)
  fetch_app(
    mnist-8
    validated/vision/classification/mnist/model/mnist-8.tar.gz
    model
    2f47e338faddbb30488abd83e8179ce44882de465165bb3a5b2640719f64f70c
  )
  fetch_app(
    mobilenetv2-7
    validated/vision/classification/mobilenet/model/mobilenetv2-7.tar.gz
    mobilenetv2-7
    b463ad62dae99f13afd88549ca7d43e9bda6876614f3592ebb41177e1db0fcc5
  )
endif()
