// RUN: onnx-mlir-opt --maccel=Sat --sat-lower %s | FileCheck %s

module {
  func.func @main(%left: tensor<i64>, %right: tensor<i64>) -> tensor<i64> {
    %0 = builtin.unrealized_conversion_cast %left : tensor<i64> to tensor<!sat.int<5>>
    %1 = builtin.unrealized_conversion_cast %right : tensor<i64> to tensor<!sat.int<5>>
    %2 = "sat.add"(%0, %1) : (tensor<!sat.int<5>>, tensor<!sat.int<5>>) -> tensor<!sat.int<5>>
    %3 = builtin.unrealized_conversion_cast %2 : tensor<!sat.int<5>> to tensor<i64>
    return %3 : tensor<i64>
  }
}

// CHECK: func.func private @sat_add_5_scalar
// CHECK-SAME: attributes {sat.generated, sat.width = 5 : i64}
// CHECK: "onnx.Add"
// CHECK: "onnx.Max"
// CHECK: "onnx.Min"
// CHECK-NOT: !sat.int
