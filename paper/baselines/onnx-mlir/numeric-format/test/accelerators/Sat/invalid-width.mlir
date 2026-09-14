// RUN: not onnx-mlir-opt --maccel=Sat %s 2>&1 | FileCheck %s

module {
  func.func @invalid(%arg: tensor<!sat.int<1>>) {
    return
  }
}

// CHECK: sat integer width must be between 2 and 63
