module attributes {llvm.data_layout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32", llvm.target_triple = "arm64-apple-darwin24.6.0", "onnx-mlir.compile_options" = "--EmitONNXBasic -o imported model.onnx", "onnx-mlir.compiler_version" = "onnx-mlir version 0.4.2 (4a13c34a)", "onnx-mlir.symbol-postfix" = "imported"} {
  func.func @main_graph(%arg0: tensor<i64> {onnx.name = "scalar_0_left"}, %arg1: tensor<i64> {onnx.name = "scalar_0_right"}, %arg2: tensor<i64> {onnx.name = "scalar_1_left"}, %arg3: tensor<i64> {onnx.name = "scalar_1_right"}, %arg4: tensor<i64> {onnx.name = "scalar_2_left"}, %arg5: tensor<i64> {onnx.name = "scalar_2_right"}, %arg6: tensor<i64> {onnx.name = "scalar_3_left"}, %arg7: tensor<i64> {onnx.name = "scalar_3_right"}, %arg8: tensor<4xi64> {onnx.name = "tensor_left"}, %arg9: tensor<4xi64> {onnx.name = "tensor_right"}) -> (tensor<i64> {onnx.name = "scalar_0_result"}, tensor<i64> {onnx.name = "scalar_1_result"}, tensor<i64> {onnx.name = "scalar_2_result"}, tensor<i64> {onnx.name = "scalar_3_result"}, tensor<4xi64> {onnx.name = "tensor_result"}) {
    %0 = "onnx.Add"(%arg0, %arg1) {onnx_node_name = "scalar_0_add"} : (tensor<i64>, tensor<i64>) -> tensor<i64>
    %1 = "onnx.Add"(%arg2, %arg3) {onnx_node_name = "scalar_1_add"} : (tensor<i64>, tensor<i64>) -> tensor<i64>
    %2 = "onnx.Add"(%arg4, %arg5) {onnx_node_name = "scalar_2_add"} : (tensor<i64>, tensor<i64>) -> tensor<i64>
    %3 = "onnx.Add"(%arg6, %arg7) {onnx_node_name = "scalar_3_add"} : (tensor<i64>, tensor<i64>) -> tensor<i64>
    %4 = "onnx.Add"(%arg8, %arg9) {onnx_node_name = "tensor_add"} : (tensor<4xi64>, tensor<4xi64>) -> tensor<4xi64>
    onnx.Return %0, %1, %2, %3, %4 : tensor<i64>, tensor<i64>, tensor<i64>, tensor<i64>, tensor<4xi64>
  }
  "onnx.EntryPoint"() <{func = @main_graph}> : () -> ()
}
