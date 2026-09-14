module attributes {llvm.data_layout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32", llvm.target_triple = "arm64-apple-darwin24.6.0", "onnx-mlir.compile_options" = "--ops-for-call=MatMul --O0 --EmitMLIR -o matmul model.onnx", "onnx-mlir.compiler_version" = "onnx-mlir version 0.4.2 (4a13c34a)", "onnx-mlir.op_stats" = "{\0A  \22func.return.2D\22 : 1,\0A  \22onnx.MatMul.2D.2D\22 : 1\0A}\0A", "onnx-mlir.symbol-postfix" = "matmul"} {
  func.func @main_graph(%arg0: memref<2x3xf32> {onnx.name = "a"}, %arg1: memref<3x2xf32> {onnx.name = "b"}) -> (memref<2x2xf32> {onnx.name = "c"}) attributes {llvm.emit_c_interface} {
    %cst = arith.constant 0.000000e+00 : f32
    %alloc = memref.alloc() {alignment = 16 : i64} : memref<2x2xf32>
    affine.for %arg2 = 0 to 2 {
      affine.for %arg3 = 0 to 2 {
        %0 = affine.for %arg4 = 0 to 3 iter_args(%arg5 = %cst) -> (f32) {
          %1 = affine.load %arg0[%arg2, %arg4] : memref<2x3xf32>
          %2 = affine.load %arg1[%arg4, %arg3] : memref<3x2xf32>
          %3 = arith.mulf %1, %2 : f32
          %4 = arith.addf %arg5, %3 : f32
          affine.yield %4 : f32
        }
        affine.store %0, %alloc[%arg2, %arg3] : memref<2x2xf32>
      }
    }
    return %alloc : memref<2x2xf32>
  }
  "krnl.entry_point"() {func = @main_graph, numInputs = 2 : i32, numOutputs = 1 : i32, signature = "[    { \22type\22 : \22f32\22 , \22dims\22 : [2 , 3] , \22name\22 : \22a\22 }\0A ,    { \22type\22 : \22f32\22 , \22dims\22 : [3 , 2] , \22name\22 : \22b\22 }\0A\0A]\00@[   { \22type\22 : \22f32\22 , \22dims\22 : [2 , 2] , \22name\22 : \22c\22 }\0A\0A]\00"} : () -> ()
}
