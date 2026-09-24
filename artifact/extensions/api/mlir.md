# MLIR extension interface

## Fused quantized convolution entry points

For `vert-fused-op`, implement `void transform(mlir::ModuleOp)` and
`llvm::json::Object analyze(mlir::ModuleOp)`. The second entry receives the
verified transformed IR. Inspect `func::CallOp` operands and their defining
operations, check `hasOneUse()`, and read `RankedTensorType::getShape()`.
Scale attributes are f64 `FloatAttr`; the zero-point is an i64 `IntegerAttr`.
Copy metadata without replacing the new call's `callee` attribute.

The input is a typed SSA chain `qconv(x,w) → bias(conv,b) → requantize → relu`.
The qconv operands are NHWC i8 activations and HWIO i8 weights; its result
and the biased result are i32 tensors. Requantize and ReLU return i8 tensors.
The qconv attributes are `layout="NHWC"` and `kernel_layout="HWIO"`;
requantize carries `acc_scale`, `out_scale`, and `output_zero`; ReLU also
carries `output_zero`. A matching `fused_qconv_relu(x,w,b)` declaration
is supplied. Fuse only when all three intermediate values have one use.
Preserve shared chains, output uses, tensor types, and all five fused attributes.

The emission result has exactly `symbol="task_kernel"` and `source`,
a complete C99 translation unit exporting
`int task_kernel(const int8_t *x, const int8_t *w, const int32_t *b, int8_t *y)`.
Arrays are contiguous NHWC, HWIO, channel bias, and NHWC output respectively.
Return zero on valid inputs; preserve inputs and output boundaries. Specialize
shapes and quantization metadata, not tensor contents: runtime vectors are
provided only to the compiled executable. Apply valid cross-correlation with
unit stride/dilation, no padding, and zero input/weight zero-points. Add i32
bias, compute `acc * acc_scale / out_scale`, round ties to even, add the
output zero-point, and clamp to `[output_zero,127]`. The driver selects
round-to-nearest-even and checks exact signed output bytes against an independent
integer-accumulation oracle. The shared-chain negative case is checked
structurally and does not request a fused kernel.

## Signed-int4 vertical entry points

For `vert-int4`, implement `void transform(mlir::ModuleOp)` followed by
`llvm::json::Object analyze(mlir::ModuleOp)`. The source `subject` takes two
rank-one i4 tensors and returns a saturated i4 sum and packed i8 bytes. Replace
`qint4_add` with the declared `sext_i4_i8` calls, `add_i8`, `clamp_i8`
(i64 attributes `lower=-8`, `upper=7`), and `trunc_i8_i4`; retain `qint4_pack`
and both result uses. Validate the wide `values` array of `qint4_literal`
before truncation. Reject invalid literals with the specified diagnostic.

`analyze` receives verified lowered IR. Return exactly `range=[-8,7]`,
`symbol="task_kernel"`, and `source` containing a complete C99 translation unit
exporting `int task_kernel(const int8_t*, const int8_t*, int8_t*, uint8_t*, size_t)`.
Arguments are lhs, rhs, unpacked signed results, packed bytes, and element count.
Return zero on valid inputs; preserve input buffers and output boundaries.
Count zero uses null pointers. Runtime vectors are not present in the IR;
the oracle tests every signed-nibble operand pair at each lane.

## Input format entry point

For `vert-input-format`, implement
`mlir::OwningOpRef<mlir::ModuleOp> readExtension(llvm::StringRef source, mlir::MLIRContext &context)`.
Construct a native module containing a single-block `func.func @subject` with
typed tensor arguments, typed `func.call` operations, and `func.return`.
Declare the called primitives privately. Use `splat`, `add`, `mul`, and `relu`,
or distinguish signatures with `splat__typeN`, `add__typeN`, `mul__typeN`, and
`relu__typeN`, where N is a nonnegative decimal integer. Splat constants carry
a `value` attribute interpreted at the result element type, including f32 rounding.
The driver verifies the module and its print/parse/print stability; a separate
observer compares its complete typed dataflow. Reject malformed input by throwing
`std::runtime_error` with the specified diagnostic. Returning a report is not import.

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

For `con-quant-expand`, qadd attributes include three floating scales and the
`zeros` array. Construct an intermediate `RankedTensorType` with f32 elements
and the same shape. The supplied targets are `dequantize` (i8 to f32), `add`
(f32), and `quantize` (f32 to i8). Copy each conversion's corresponding `scale`
and `zero` attributes. Preserve result users and reject nonpositive scales.

For `con-layout-legalize`, permute `RankedTensorType` dimensions for NHWC to
NCHW and HWIO to OIHW. Declared targets are `transpose_input`,
`transpose_weight`, `conv2d_nchw`, and `transpose_output`. Attach each
transpose's `perm` using `getI64ArrayAttr`. Copy `stride`, `pad`, and
`dilation` to the convolution; padding order is top, left, bottom, right.
Transpose the result back to NHWC and replace every source result use.
Reject mismatched input/weight channels before mutation.

For `rew-conv-bias-relu`, follow `getDefiningOp<func::CallOp>()` from ReLU
through BiasAdd to convolution; require `hasOneUse()` for both intermediate
results. Check rank-one channel bias and the layout's bias axis. Create the
declared `fused_conv_bias_relu` with input, weight, bias operands. Copy all
convolution attributes except `callee`, preserving the new target symbol.
Replace final users, then erase ReLU, BiasAdd, and convolution.

For `emit-kernel-wrapper`, read the `kernel` StringAttr from `study.request`
and return an `llvm::json::Object` with exactly `symbol` and `source`. Source
is a complete C99 translation unit exporting `task_kernel` with arguments
`(const float*, float*, size_t)`. Runtime vectors are not supplied to the
emitter. The oracle compiles and executes independent-buffer, in-place, and
zero-count calls, checking exact f32 bits and positive-zero ReLU output.

## Type definition entry point

For `def-target-intrinsic`, register `extension.dot4_i8` and export
`Operation *constructExtension(OpBuilder &, ValueRange, ArrayRef<NamedAttribute>)`.
The constructor receives three operands and no attributes. Return a detached
registered operation that consumes these operands and produces one `i32`.
Its verifier checks both fixed, one-dimensional `vector<4xi8>` operands and
the accumulator independently. The driver verifies and round-trips the operation;
the observer checks operand identity and interprets fixed dot-product semantics.
This task does not require a machine-code lowering.

For `def-layout-attribute`, use the same registration and construction entry
points with one tensor operand and two `StringAttr` values, `src` and `dst`.
Construct registered `extension.relayout` with its rank-four result inferred
from the layout permutation. The driver derives the function return type
from the constructed result. Preserve element type and both attributes;
reject invalid rank with `rank-mismatch` and unknown layouts with
`invalid-layout`. The fixed observer checks operand identity and the printed
result type; the fixture supplies no expected output shape.

For `def-quantized-op`, register native `extension.qadd` and export
`Operation *constructExtension(OpBuilder &, ValueRange, ArrayRef<NamedAttribute>)`.
The constructor receives the two subject arguments and six quantization
attributes, without an expected-result-type argument. Build the registered
operation and infer its result from the lhs tensor. Its verifier checks equal
i8 tensor shapes, finite positive f64 scales, and i32 zero points. Attributes
are named `lhs_scale`, `rhs_scale`, `output_scale`, `lhs_zero`, `rhs_zero`, and
`output_zero`. The driver verifies and round-trips the constructed IR; a
separate fixed observer checks its operand/result wiring and attributes.

For `def-parametric-type`, export `void registerExtension(MLIRContext&)`.
Register a dialect named `extension` containing `extension.fx` backed by
`Type::TypeBase` and a storage key `(width, frac)`. Implement dialect
`parseType`/`printType` for `!extension.fx<width,frac>`. Use `getChecked`
and static `verifyInvariants` to enforce the parameter bounds and emit
`invalid-type-parameter`. A hand-written method named only `verify` is not
automatically called by the checked TypeBase constructor. Anonymous-namespace
classes need `MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID`.

The fixed driver registers the dialect before parsing, verifies the fixture,
prints and reparses it, verifies again, and reports actual argument types.
No candidate `analyze` function is called in definition mode.
