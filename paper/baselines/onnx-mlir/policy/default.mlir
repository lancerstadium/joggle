module attributes {llvm.data_layout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32", llvm.target_triple = "arm64-apple-darwin24.6.0", "onnx-mlir.compile_options" = "--O0 --EmitMLIR -o default model.onnx", "onnx-mlir.compiler_version" = "onnx-mlir version 0.4.2 (4a13c34a)", "onnx-mlir.op_stats" = "{\0A  \22func.return.1D\22 : 1,\0A  \22onnx.Add.1D.1D\22 : 3\0A}\0A", "onnx-mlir.symbol-postfix" = "default"} {
  func.func @main_graph(%arg0: memref<4xf32> {onnx.name = "a"}, %arg1: memref<4xf32> {onnx.name = "b"}, %arg2: memref<4xf32> {onnx.name = "c"}, %arg3: memref<4xf32> {onnx.name = "d"}) -> (memref<4xf32> {onnx.name = "result"}) attributes {llvm.emit_c_interface} {
    %alloc = memref.alloc() {alignment = 16 : i64} : memref<4xf32>
    affine.for %arg4 = 0 to 4 {
      %0 = affine.load %arg0[%arg4] : memref<4xf32>
      %1 = affine.load %arg1[%arg4] : memref<4xf32>
      %2 = arith.addf %0, %1 : f32
      %3 = affine.load %arg2[%arg4] : memref<4xf32>
      %4 = arith.addf %2, %3 : f32
      %5 = affine.load %arg3[%arg4] : memref<4xf32>
      %6 = arith.addf %4, %5 : f32
      affine.store %6, %alloc[%arg4] : memref<4xf32>
    }
    return %alloc : memref<4xf32>
  }
  "krnl.entry_point"() {func = @main_graph, numInputs = 4 : i32, numOutputs = 1 : i32, signature = "[    { \22type\22 : \22f32\22 , \22dims\22 : [4] , \22name\22 : \22a\22 }\0A ,    { \22type\22 : \22f32\22 , \22dims\22 : [4] , \22name\22 : \22b\22 }\0A ,    { \22type\22 : \22f32\22 , \22dims\22 : [4] , \22name\22 : \22c\22 }\0A ,    { \22type\22 : \22f32\22 , \22dims\22 : [4] , \22name\22 : \22d\22 }\0A\0A]\00@[   { \22type\22 : \22f32\22 , \22dims\22 : [4] , \22name\22 : \22result\22 }\0A\0A]\00"} : () -> ()
}
