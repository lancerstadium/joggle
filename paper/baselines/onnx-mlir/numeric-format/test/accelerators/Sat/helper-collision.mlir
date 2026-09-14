// RUN: not onnx-mlir-opt --maccel=Sat --sat-lower %s 2>&1 | FileCheck %s

module {
  func.func private @sat_add_5_scalar(i64) -> i64
  func.func @main(%left: tensor<i64>, %right: tensor<i64>) -> tensor<i64> {
    %0 = builtin.unrealized_conversion_cast %left : tensor<i64> to tensor<!sat.int<5>>
    %1 = builtin.unrealized_conversion_cast %right : tensor<i64> to tensor<!sat.int<5>>
    %2 = "sat.add"(%0, %1) : (tensor<!sat.int<5>>, tensor<!sat.int<5>>) -> tensor<!sat.int<5>>
    %3 = builtin.unrealized_conversion_cast %2 : tensor<!sat.int<5>> to tensor<i64>
    return %3 : tensor<i64>
  }
}

// CHECK: collides with generated sat helper 'sat_add_5_scalar'
