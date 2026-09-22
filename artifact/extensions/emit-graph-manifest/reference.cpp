#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <string>
#include <vector>

static llvm::json::Value attribute(mlir::Attribute attr) {
  if (auto value = llvm::dyn_cast<mlir::IntegerAttr>(attr)) {
    if (value.getType().isInteger(1)) return value.getInt() != 0;
    return value.getInt();
  }
  if (auto value = llvm::dyn_cast<mlir::FloatAttr>(attr)) return value.getValueAsDouble();
  if (auto value = llvm::dyn_cast<mlir::StringAttr>(attr)) return value.getValue();
  if (auto values = llvm::dyn_cast<mlir::ArrayAttr>(attr)) {
    llvm::json::Array out;
    for (auto value : values) out.push_back(attribute(value));
    return out;
  }
  llvm_unreachable("unsupported fixture attribute");
}

llvm::json::Object analyze(mlir::ModuleOp module) {
  auto subject = module.lookupSymbol<mlir::func::FuncOp>("subject");
  llvm::DenseMap<mlir::Value, std::string> ids;
  const auto define = [&](mlir::Value value) -> llvm::json::Object {
    std::string id = "v" + std::to_string(ids.size());
    ids[value] = id;
    auto type = llvm::cast<mlir::RankedTensorType>(value.getType());
    std::string element;
    llvm::raw_string_ostream stream(element);
    type.getElementType().print(stream);
    llvm::json::Array shape;
    for (int64_t axis = 0; axis < type.getRank(); ++axis)
      shape.push_back(type.isDynamicDim(axis) ? int64_t(-1) : type.getDimSize(axis));
    return llvm::json::Object{{"id", id}, {"type", llvm::json::Object{
        {"element", element}, {"shape", std::move(shape)}}}};
  };
  llvm::json::Array inputs, nodes, outputs;
  for (auto value : subject.getArguments()) inputs.push_back(define(value));
  for (auto &op : subject.getBody().front()) {
    if (auto call = llvm::dyn_cast<mlir::func::CallOp>(op)) {
      llvm::json::Array operands, results, attrs;
      for (auto value : call.getOperands()) operands.push_back(ids.lookup(value));
      for (auto value : call.getResults()) results.push_back(define(value));
      std::vector<mlir::NamedAttribute> ordered;
      for (auto attr : call->getAttrs())
        if (attr.getName().getValue() != "callee") ordered.push_back(attr);
      std::sort(ordered.begin(), ordered.end(), [](auto a, auto b) {
        return a.getName().getValue() < b.getName().getValue();
      });
      for (auto attr : ordered)
        attrs.push_back(llvm::json::Array{attr.getName().getValue(), attribute(attr.getValue())});
      nodes.push_back(llvm::json::Object{
          {"id", "n" + std::to_string(nodes.size())}, {"op", call.getCallee()},
          {"inputs", std::move(operands)}, {"results", std::move(results)},
          {"attrs", std::move(attrs)}});
    } else if (auto ret = llvm::dyn_cast<mlir::func::ReturnOp>(op)) {
      for (auto value : ret.getOperands()) outputs.push_back(ids.lookup(value));
    }
  }
  return llvm::json::Object{{"schema_version", 1}, {"inputs", std::move(inputs)},
          {"nodes", std::move(nodes)}, {"outputs", std::move(outputs)}};
}
