# MLIR extension interface

## Analysis entry point

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

## Native rewrite entry point

For rewrite tasks implement `void transform(mlir::ModuleOp module)`. The driver
verifies and prints the modified module; a separate process parses that output
and observes its SSA graph. Do not return a JSON description of a rewrite.

`rew-add-zero` supplies `func.func @subject` with tensor-typed `func.call`
operations named `splat` and `add`, representing the same small tensor dialect
used by all systems. A splat carries scalar `value` (`IntegerAttr` or
`FloatAttr`); add carries Boolean `no_signed_zeros`. There is no request map.
Calls refer to private declarations and use native SSA def-use chains.

Find the function with `module.lookupSymbol<mlir::func::FuncOp>("subject")`.
Use `getOperand`, `getResult`, `getDefiningOp<mlir::func::CallOp>`, and
`RankedTensorType::getElementType` to inspect it. `result.replaceAllUsesWith(v)`
redirects users, `op.erase()` removes an operation, and `value.use_empty()`
checks liveness. Use `llvm::make_early_inc_range` or snapshot handles while
erasing. `FloatAttr::getValue()` returns an APFloat with `isZero()` and
`isNegative()`. The observer checks structure, types, live constants, and
numerical results including strict signed-zero behavior.

`rew-redundant-cast` uses the same rewrite entry with unary `cast_A_B`
calls (for example `cast_i8_i16`). Tensor operand/result types describe element
types and shape; there is no request metadata. `RankedTensorType::getShape()`
compares shapes, `IntegerType::getWidth()` reads integer width, and `Type`
provides `isF32()`/`isF64()`. Compare full original and final types before
replacement and preserve an inner cast that has other users. Scoring includes
integer wraparound, non-integral floats, precision loss, and signed-zero bits.

For `rew-transpose-pair`, unary callees begin with `transpose_`; their suffix
encodes element type and source/result shapes. `getAttrOfType<ArrayAttr>("perm")`
returns permutation entries as `IntegerAttr`. Validate indices and tensor
result dimensions before rewriting, including rank-zero lists. Preserve shared
inner results. Invalid permutations must print `invalid-permutation` to stderr
and exit with a positive status before emitting IR (for example via `std::exit`).

`con-instruction-select` uses the same mutation entry. Both `matmul` and the
matching target `mma_m16n16k16` are declared. Inspect ranked operand/result
types; `CallOp::setCallee` changes the target without replacing values.
`Builder::getI64ArrayAttr` constructs the `tiles` metadata. Preserve other keys
and every result user; negative or zero dimensions cannot select the target.

For `con-gelu-expand`, target functions `splat`, `mul`, `div`, `add`, and `erf`
are declared with the subject tensor type. Set `OpBuilder`'s insertion point
before the source call and create typed `func::CallOp` operations. Attach a
splat's `value` with `Builder::getF64FloatAttr`, replace all source result uses,
and erase the old GELU call. Reject unsupported types before emitting IR.
