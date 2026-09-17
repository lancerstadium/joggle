module attributes {llvm.data_layout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32", llvm.target_triple = "arm64-apple-darwin24.6.0", "onnx-mlir.accels" = ["Policy-0x100"], "onnx-mlir.compile_options" = "--maccel=Policy --EmitONNXIR --policy-call-weight=4 --policy-other-weight=1 --policy-report -o measure-4-1 measure.mlir", "onnx-mlir.compiler_version" = "onnx-mlir version 0.4.2 (4a13c34a)", "onnx-mlir.symbol-postfix" = "measure-4-1"} {
  func.func private @opaque(i32) -> i32
  func.func @main(%arg0: i32) -> i32 {
    %0 = call @opaque(%arg0) : (i32) -> i32
    return %0 : i32
  }
}
