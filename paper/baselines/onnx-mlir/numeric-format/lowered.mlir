module attributes {llvm.data_layout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32", llvm.target_triple = "arm64-apple-darwin24.6.0", "onnx-mlir.accels" = ["Sat-0x100"], "onnx-mlir.compile_options" = "--maccel=Sat --sat-format-map=format-map.json --sat-report --EmitONNXIR -o sat model.onnx", "onnx-mlir.compiler_version" = "onnx-mlir version 0.4.2 (4a13c34a)", "onnx-mlir.symbol-postfix" = "sat"} {
  func.func @main_graph(%arg0: tensor<i64> {onnx.name = "scalar_0_left"}, %arg1: tensor<i64> {onnx.name = "scalar_0_right"}, %arg2: tensor<i64> {onnx.name = "scalar_1_left"}, %arg3: tensor<i64> {onnx.name = "scalar_1_right"}, %arg4: tensor<i64> {onnx.name = "scalar_2_left"}, %arg5: tensor<i64> {onnx.name = "scalar_2_right"}, %arg6: tensor<i64> {onnx.name = "scalar_3_left"}, %arg7: tensor<i64> {onnx.name = "scalar_3_right"}, %arg8: tensor<4xi64> {onnx.name = "tensor_left"}, %arg9: tensor<4xi64> {onnx.name = "tensor_right"}) -> (tensor<i64> {onnx.name = "scalar_0_result"}, tensor<i64> {onnx.name = "scalar_1_result"}, tensor<i64> {onnx.name = "scalar_2_result"}, tensor<i64> {onnx.name = "scalar_3_result"}, tensor<4xi64> {onnx.name = "tensor_result"}) {
    %0 = call @sat_add_5_scalar(%arg0, %arg1) : (tensor<i64>, tensor<i64>) -> tensor<i64>
    %1 = call @sat_add_5_scalar(%arg2, %arg3) : (tensor<i64>, tensor<i64>) -> tensor<i64>
    %2 = call @sat_add_8_scalar(%arg4, %arg5) : (tensor<i64>, tensor<i64>) -> tensor<i64>
    %3 = call @sat_add_12_scalar(%arg6, %arg7) : (tensor<i64>, tensor<i64>) -> tensor<i64>
    %4 = call @sat_add_5_4(%arg8, %arg9) : (tensor<4xi64>, tensor<4xi64>) -> tensor<4xi64>
    return %0, %1, %2, %3, %4 : tensor<i64>, tensor<i64>, tensor<i64>, tensor<i64>, tensor<4xi64>
  }
  "onnx.EntryPoint"() <{func = @main_graph}> : () -> ()
  func.func private @sat_add_5_scalar(%arg0: tensor<i64>, %arg1: tensor<i64>) -> tensor<i64> attributes {sat.generated, sat.width = 5 : i64} {
    %0 = onnx.Constant dense<15> : tensor<i64>
    %1 = onnx.Constant dense<-16> : tensor<i64>
    %2 = "onnx.Add"(%arg0, %arg1) {onnx_node_name = "scalar_0_add_0"} : (tensor<i64>, tensor<i64>) -> tensor<i64>
    %3 = "onnx.Max"(%2, %1) {onnx_node_name = "scalar_0_add_1"} : (tensor<i64>, tensor<i64>) -> tensor<i64>
    %4 = "onnx.Min"(%3, %0) {onnx_node_name = "scalar_0_add_2"} : (tensor<i64>, tensor<i64>) -> tensor<i64>
    return %4 : tensor<i64>
  }
  func.func private @sat_add_8_scalar(%arg0: tensor<i64>, %arg1: tensor<i64>) -> tensor<i64> attributes {sat.generated, sat.width = 8 : i64} {
    %0 = onnx.Constant dense<127> : tensor<i64>
    %1 = onnx.Constant dense<-128> : tensor<i64>
    %2 = "onnx.Add"(%arg0, %arg1) {onnx_node_name = "scalar_2_add_3"} : (tensor<i64>, tensor<i64>) -> tensor<i64>
    %3 = "onnx.Max"(%2, %1) {onnx_node_name = "scalar_2_add_4"} : (tensor<i64>, tensor<i64>) -> tensor<i64>
    %4 = "onnx.Min"(%3, %0) {onnx_node_name = "scalar_2_add_5"} : (tensor<i64>, tensor<i64>) -> tensor<i64>
    return %4 : tensor<i64>
  }
  func.func private @sat_add_12_scalar(%arg0: tensor<i64>, %arg1: tensor<i64>) -> tensor<i64> attributes {sat.generated, sat.width = 12 : i64} {
    %0 = onnx.Constant dense<2047> : tensor<i64>
    %1 = onnx.Constant dense<-2048> : tensor<i64>
    %2 = "onnx.Add"(%arg0, %arg1) {onnx_node_name = "scalar_3_add_6"} : (tensor<i64>, tensor<i64>) -> tensor<i64>
    %3 = "onnx.Max"(%2, %1) {onnx_node_name = "scalar_3_add_7"} : (tensor<i64>, tensor<i64>) -> tensor<i64>
    %4 = "onnx.Min"(%3, %0) {onnx_node_name = "scalar_3_add_8"} : (tensor<i64>, tensor<i64>) -> tensor<i64>
    return %4 : tensor<i64>
  }
  func.func private @sat_add_5_4(%arg0: tensor<4xi64>, %arg1: tensor<4xi64>) -> tensor<4xi64> attributes {sat.generated, sat.width = 5 : i64} {
    %0 = onnx.Constant dense<15> : tensor<4xi64>
    %1 = onnx.Constant dense<-16> : tensor<4xi64>
    %2 = "onnx.Add"(%arg0, %arg1) {onnx_node_name = "tensor_add_9"} : (tensor<4xi64>, tensor<4xi64>) -> tensor<4xi64>
    %3 = "onnx.Max"(%2, %1) {onnx_node_name = "tensor_add_10"} : (tensor<4xi64>, tensor<4xi64>) -> tensor<4xi64>
    %4 = "onnx.Min"(%3, %0) {onnx_node_name = "tensor_add_11"} : (tensor<4xi64>, tensor<4xi64>) -> tensor<4xi64>
    return %4 : tensor<4xi64>
  }
}
