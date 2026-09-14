module attributes {llvm.data_layout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32", llvm.target_triple = "arm64-apple-darwin24.6.0", "onnx-mlir.compile_options" = "--EmitONNXBasic -o imported model.onnx", "onnx-mlir.compiler_version" = "onnx-mlir version 0.4.2 (4a13c34a)", "onnx-mlir.symbol-postfix" = "imported"} {
  func.func @main_graph(%arg0: tensor<i64> {onnx.name = "scalar_0_left"}, %arg1: tensor<i64> {onnx.name = "scalar_0_right"}, %arg2: tensor<i64> {onnx.name = "scalar_1_left"}, %arg3: tensor<i64> {onnx.name = "scalar_1_right"}, %arg4: tensor<i64> {onnx.name = "scalar_2_left"}, %arg5: tensor<i64> {onnx.name = "scalar_2_right"}, %arg6: tensor<i64> {onnx.name = "scalar_3_left"}, %arg7: tensor<i64> {onnx.name = "scalar_3_right"}, %arg8: tensor<4xi64> {onnx.name = "tensor_left"}, %arg9: tensor<4xi64> {onnx.name = "tensor_right"}) -> (tensor<i64> {onnx.name = "scalar_0_result"}, tensor<i64> {onnx.name = "scalar_1_result"}, tensor<i64> {onnx.name = "scalar_2_result"}, tensor<i64> {onnx.name = "scalar_3_result"}, tensor<4xi64> {onnx.name = "tensor_result"}) {
    %0 = builtin.unrealized_conversion_cast %arg0 : tensor<i64> to tensor<!sat.int<5>>
    %1 = builtin.unrealized_conversion_cast %arg1 : tensor<i64> to tensor<!sat.int<5>>
    %2 = "sat.add"(%0, %1) : (tensor<!sat.int<5>>, tensor<!sat.int<5>>) -> tensor<!sat.int<5>>
    %3 = builtin.unrealized_conversion_cast %2 : tensor<!sat.int<5>> to tensor<i64>
    %4 = builtin.unrealized_conversion_cast %arg2 : tensor<i64> to tensor<!sat.int<5>>
    %5 = builtin.unrealized_conversion_cast %arg3 : tensor<i64> to tensor<!sat.int<5>>
    %6 = "sat.add"(%4, %5) : (tensor<!sat.int<5>>, tensor<!sat.int<5>>) -> tensor<!sat.int<5>>
    %7 = builtin.unrealized_conversion_cast %6 : tensor<!sat.int<5>> to tensor<i64>
    %8 = builtin.unrealized_conversion_cast %arg4 : tensor<i64> to tensor<!sat.int<8>>
    %9 = builtin.unrealized_conversion_cast %arg5 : tensor<i64> to tensor<!sat.int<8>>
    %10 = "sat.add"(%8, %9) : (tensor<!sat.int<8>>, tensor<!sat.int<8>>) -> tensor<!sat.int<8>>
    %11 = builtin.unrealized_conversion_cast %10 : tensor<!sat.int<8>> to tensor<i64>
    %12 = builtin.unrealized_conversion_cast %arg6 : tensor<i64> to tensor<!sat.int<12>>
    %13 = builtin.unrealized_conversion_cast %arg7 : tensor<i64> to tensor<!sat.int<12>>
    %14 = "sat.add"(%12, %13) : (tensor<!sat.int<12>>, tensor<!sat.int<12>>) -> tensor<!sat.int<12>>
    %15 = builtin.unrealized_conversion_cast %14 : tensor<!sat.int<12>> to tensor<i64>
    %16 = builtin.unrealized_conversion_cast %arg8 : tensor<4xi64> to tensor<4x!sat.int<5>>
    %17 = builtin.unrealized_conversion_cast %arg9 : tensor<4xi64> to tensor<4x!sat.int<5>>
    %18 = "sat.add"(%16, %17) : (tensor<4x!sat.int<5>>, tensor<4x!sat.int<5>>) -> tensor<4x!sat.int<5>>
    %19 = builtin.unrealized_conversion_cast %18 : tensor<4x!sat.int<5>> to tensor<4xi64>
    onnx.Return %3, %7, %11, %15, %19 : tensor<i64>, tensor<i64>, tensor<i64>, tensor<i64>, tensor<4xi64>
  }
  "onnx.EntryPoint"() <{func = @main_graph}> : () -> ()
}

