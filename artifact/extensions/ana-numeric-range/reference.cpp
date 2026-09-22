#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/Support/JSON.h"
#include <algorithm>
#include <array>
#include <vector>

llvm::json::Object analyze(mlir::ModuleOp module) {
  using namespace mlir;
  const auto request = module->getAttrOfType<DictionaryAttr>("study.request");
  std::vector<DictionaryAttr> steps;
  if (auto graph = llvm::dyn_cast_or_null<ArrayAttr>(request.get("graph")))
    for (auto step : graph)
      steps.push_back(llvm::cast<DictionaryAttr>(step));
  else
    steps.push_back(request);
  const auto number = [](Attribute attr) -> double {
    if (auto value = llvm::dyn_cast<FloatAttr>(attr))
      return value.getValueAsDouble();
    return llvm::cast<IntegerAttr>(attr).getInt();
  };
  const auto interval = [&](Attribute attr, std::array<double, 2> fallback) {
    if (!attr) return fallback;
    auto values = llvm::cast<ArrayAttr>(attr);
    return std::array<double, 2>{number(values[0]), number(values[1])};
  };
  std::array<double, 2> result{0, 0};
  for (auto step : steps) {
    auto op = llvm::cast<StringAttr>(step.get("op")).getValue();
    auto lhs = interval(step.get("lhs"), interval(step.get("value"), result));
    auto rhs = interval(step.get("rhs"), {0, 0});
    auto [a, b] = lhs;
    auto [c, d] = rhs;
    if (a > b || c > d)
      return llvm::json::Object{{"error", "invalid-interval"}, {"phase", "build"}};
    if (op == "add") result = {a + c, b + d};
    else if (op == "mul")
      result = {std::min({a*c, a*d, b*c, b*d}), std::max({a*c, a*d, b*c, b*d})};
    else if (op == "relu") result = {std::max(a, 0.0), std::max(b, 0.0)};
    else if (op == "clamp") {
      double lower = number(step.get("lower")), upper = number(step.get("upper"));
      if (lower > upper)
        return llvm::json::Object{{"error", "invalid-interval"}, {"phase", "build"}};
      result = {std::clamp(a, lower, upper), std::clamp(b, lower, upper)};
    } else
      return llvm::json::Object{{"error", "unsupported-op"}, {"phase", "build"}};
  }
  return llvm::json::Object{{"interval", llvm::json::Array{result[0], result[1]}}};
}
