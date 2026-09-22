# MLIR analysis extension interface

Edit the supplied C++ implementation. Its public entry point is:

```cpp
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/Support/JSON.h"

llvm::json::Object analyze(mlir::ModuleOp module) {
  return {};
}
```

For attribute-request tasks, the driver parses a builtin module with a
`study.request` dictionary attribute.
Read that native IR attribute with:

```cpp
auto request = module->getAttrOfType<mlir::DictionaryAttr>("study.request");
auto items = llvm::cast<mlir::ArrayAttr>(request.get("items"));
auto count = items.size();
auto first = llvm::cast<mlir::IntegerAttr>(items[0]).getInt();
```

Arrays are iterable and indexable. Integer attributes in this interface use
`i64`. Standard C++ containers, control flow, and arithmetic are available.
Construct JSON objects and arrays using LLVM's support library:

```cpp
llvm::json::Array values;
values.push_back(first);
return llvm::json::Object{{"count", int64_t(count)}, {"values", std::move(values)}};
```

The driver links the extension with MLIRParser, MLIRIR, and MLIRSupport, then
calls `analyze` and compares its JSON result to the task's expected result.
Return semantic rejection as the error dictionary specified by the task,
rather than aborting the process. Do not change the driver or fixtures.

## Native graph analysis

For `ana-fusion-match`, the registered `func` dialect represents real typed
calls, not a `study.request` dictionary. The function `subject` has a string
attribute `layout` (`NCHW` or `NHWC`). Callee symbols include `conv2d`,
`bias_add`, and `relu`; other calls may also occur.

```cpp
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinTypes.h"
auto subject = module.lookupSymbol<mlir::func::FuncOp>("subject");
for (auto call : subject.getBody().front().getOps<mlir::func::CallOp>()) {
  auto callee = call.getCallee();
  auto producer = call.getOperand(0).getDefiningOp<mlir::func::CallOp>();
  auto singleUse = call.getResult(0).hasOneUse();
  auto type = llvm::dyn_cast<mlir::RankedTensorType>(call.getResult(0).getType());
}
```

Check a defining operation before dereferencing it; block arguments have none.
Tensor types expose `getRank()` and `getDimSize(axis)`. Report each match as
indices into the subject's `func.call` sequence, excluding its return.
Uses include every actual operand occurrence and returns. The driver also
links `MLIRFuncDialect`.

## Graph manifest emission

For `emit-graph-manifest`, `subject.getArguments()` lists inputs and
`subject.getBody().front()` lists operations. `func::CallOp` exposes
`getOperands()`, `getResults()`, and `getCallee()`; `func::ReturnOp` exposes
ordered return operands. `llvm::DenseMap<mlir::Value, ...>` can track native
SSA identities. User attributes come from `call->getAttrs()`; exclude `callee`
bookkeeping and sort the remaining `NamedAttribute` names explicitly.

`RankedTensorType` exposes `getElementType()`, `getRank()`, `getDimSize(axis)`,
and `isDynamicDim(axis)`. Types print into `llvm::raw_string_ostream`.
`IntegerAttr.getInt()`, `FloatAttr.getValueAsDouble()`, and `StringAttr.getValue()`
provide scalar values; integer attributes of type `i1` represent Booleans.
`ArrayAttr` is iterable. LLVM JSON arrays retain insertion order, allowing the
contract's sorted attribute-pair array and ordered graph references.
