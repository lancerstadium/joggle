# Pinned ONNX Model Zoo inputs used by downloads, CTest gates, and experiments.
# The caller supplies joggle_onnx_model(); this file only declares data.

set(joggle_onnx_zoo_revision 4f43949841cb55a0b98dc8fcd045431ccafd9f96)

joggle_onnx_model(
  NAME mnist-8
  SOURCE validated/vision/classification/mnist/model/mnist-8.onnx
  SHA256 2f06e72de813a8635c9bc0397ac447a601bdbfa7df4bebc278723b958831c9bf
  TEST onnx-zoo-mnist
  ARGS onnx.Conv onnx.MaxPool onnx.MatMul
  APP_SOURCE validated/vision/classification/mnist/model/mnist-8.tar.gz
  APP_TOP model
  APP_SHA256 2f47e338faddbb30488abd83e8179ce44882de465165bb3a5b2640719f64f70c
)
joggle_onnx_model(
  NAME mobilenetv2-7
  SOURCE validated/vision/classification/mobilenet/model/mobilenetv2-7.onnx
  SHA256 c1c513582d56afceff8516c73804e484c81c6a830712ab6d682253f4a3cd042f
  TEST onnx-zoo-mobilenet
  ARGS onnx.Conv
  APP_SOURCE validated/vision/classification/mobilenet/model/mobilenetv2-7.tar.gz
  APP_TOP mobilenetv2-7
  APP_SHA256 b463ad62dae99f13afd88549ca7d43e9bda6876614f3592ebb41177e1db0fcc5
)
joggle_onnx_model(
  NAME squeezenet1.1-7
  SOURCE validated/vision/classification/squeezenet/model/squeezenet1.1-7.onnx
  SHA256 1eeff551a67ae8d565ca33b572fc4b66e3ef357b0eb2863bb9ff47a918cc4088
  TEST onnx-zoo-squeezenet
  ARGS onnx.Concat onnx.Dropout
)
joggle_onnx_model(
  NAME squeezenet1.0-13-qdq
  SOURCE validated/vision/classification/squeezenet/model/squeezenet1.0-13-qdq.onnx
  SHA256 4a567dd7542ef440890d57268fabf47211174c593d7a1837bd7f16a1067169e7
  TEST onnx-zoo-squeezenet-qdq
  ARGS onnx.QuantizeLinear onnx.DequantizeLinear
)
joggle_onnx_model(
  NAME resnet18-v1-7
  SOURCE validated/vision/classification/resnet/model/resnet18-v1-7.onnx
  SHA256 4e8f8653e7a2222b3904cc3fe8e304cd8b339ce1d05fd24688162f86fb6df52c
  TEST onnx-zoo-resnet
  ARGS onnx.Add onnx.Gemm
)
joggle_onnx_model(
  NAME tinyyolov2-8
  SOURCE validated/vision/object_detection_segmentation/tiny-yolov2/model/tinyyolov2-8.onnx
  SHA256 583fb7fdc948435ceac9fa82efc7708701efe8382a859a3dd46526b155f5f2ae
  TEST onnx-zoo-tinyyolo
  ARGS onnx.LeakyRelu onnx.MaxPool
)
joggle_onnx_model(
  NAME tiny-yolov3-11
  SOURCE validated/vision/object_detection_segmentation/tiny-yolov3/model/tiny-yolov3-11.onnx
  SHA256 f715cc2d99740d22d312777e20d9de2b2ecdc250155be8fd3752ce7e8b823521
  TEST onnx-zoo-tinyyolov3
  ARGS --frontier 219 onnx.Loop
)
joggle_onnx_model(
  NAME ultraface-rfb-320
  SOURCE validated/vision/body_analysis/ultraface/models/version-RFB-320.onnx
  SHA256 34cd7e60aeff28744c657de7a3dc64e872d506741de66987f3426f2b79f88017
  TEST onnx-zoo-ultraface
  ARGS onnx.Constant onnx.Slice onnx.Softmax
)
joggle_onnx_model(
  NAME ssd-mobilenetv1-12
  SOURCE validated/vision/object_detection_segmentation/ssd-mobilenetv1/model/ssd_mobilenet_v1_12.onnx
  SHA256 b8fba5e404077d4048d27fcd1667e85e27e192eb9bf51e696c46a3acd7d21058
  TEST onnx-zoo-ssd-mobilenet
  ARGS --convert-frontier 710 onnx.Loop onnx.NonMaxSuppression onnx.Resize
)
joggle_onnx_model(
  NAME shufflenet-v2-12
  SOURCE validated/vision/classification/shufflenet/model/shufflenet-v2-12.onnx
  SHA256 ea69821b4dd374ae2f33f9710dd1229ac263d0ee5b5a46ca3521f6483e1ba035
  TEST onnx-zoo-shufflenet
  ARGS onnx.Split onnx.Concat
)
joggle_onnx_model(
  NAME densenet-12
  SOURCE validated/vision/classification/densenet-121/model/densenet-12.onnx
  SHA256 0294e7e88e5b3360de9b0fdc321baf9e6ef18b7f058c4536caae3b9f18ed9ed5
  TEST onnx-zoo-densenet
  ARGS --frontier-from-signature 0 onnx.Concat onnx.BatchNormalization
)
joggle_onnx_model(
  NAME googlenet-12
  SOURCE validated/vision/classification/inception_and_googlenet/googlenet/model/googlenet-12.onnx
  SHA256 c99c507058eaf41de8723408fdda7db8325cb57f0a89f2ee07a716d6e963e14e
  TEST onnx-zoo-googlenet
  ARGS --frontier-from-signature 0 onnx.Concat onnx.LRN onnx.Dropout
)
joggle_onnx_model(
  NAME efficientnet-lite4-11-int8
  SOURCE validated/vision/classification/efficientnet-lite4/model/efficientnet-lite4-11-int8.onnx
  SHA256 2b3cbb5077262b20df565dacddecb3724c0976c35029a87e512d13aa4eff04a2
  TEST onnx-zoo-efficientnet-int8
  ARGS --frontier-from-signature 95 onnx.QLinearConv onnx.QLinearMatMul
)
joggle_onnx_model(
  NAME efficientnet-lite4-11-qdq
  SOURCE validated/vision/classification/efficientnet-lite4/model/efficientnet-lite4-11-qdq.onnx
  SHA256 6837d0b19625d4aff8266d7197a7f3775afd82a8c40f9fd0283d52db4955566f
  TEST onnx-zoo-efficientnet-qdq
  ARGS --frontier-from-signature 0 onnx.QuantizeLinear onnx.DequantizeLinear
)
joggle_onnx_model(
  NAME bidaf-9
  SOURCE validated/text/machine_comprehension/bidirectional_attention_flow/model/bidaf-9.onnx
  SHA256 dfc317b56d065a3e297240a9e9b9118ff2260790b5850f4be2bc6ea1bcc65e80
  TEST onnx-zoo-bidaf
  ARGS --roundtrip onnx.Gather
  HEAVY
)
