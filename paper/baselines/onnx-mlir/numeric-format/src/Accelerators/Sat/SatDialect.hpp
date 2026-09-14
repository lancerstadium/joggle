/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ONNX_MLIR_SAT_DIALECT_H
#define ONNX_MLIR_SAT_DIALECT_H

#include "mlir/IR/Dialect.h"
#include "mlir/IR/Types.h"

namespace onnx_mlir::sat {

namespace detail {
struct IntTypeStorage;
} // namespace detail

class IntType final
    : public mlir::Type::TypeBase<IntType, mlir::Type, detail::IntTypeStorage> {
public:
  using Base::Base;
  static constexpr llvm::StringLiteral name = "sat.int";
  static IntType get(mlir::MLIRContext *context, unsigned width);
  unsigned getWidth() const;
};

class SatDialect final : public mlir::Dialect {
public:
  explicit SatDialect(mlir::MLIRContext *context);
  static llvm::StringRef getDialectNamespace() { return "sat"; }
  mlir::Type parseType(mlir::DialectAsmParser &parser) const final;
  void printType(mlir::Type type, mlir::DialectAsmPrinter &printer) const final;
};

} // namespace onnx_mlir::sat

#endif
