# xDSL extension interface

## Analysis entry point

Edit the supplied Python implementation. Its public entry point is:

```python
from xdsl.dialects.builtin import ArrayAttr, DictionaryAttr, IntegerAttr, ModuleOp

def analyze(module: ModuleOp) -> dict:
    return {}
```

For attribute-request tasks, the driver parses a builtin module with a
`study.request` dictionary attribute.
Read that native IR attribute with:

```python
request = module.attributes["study.request"]
assert isinstance(request, DictionaryAttr)
items = request.data["items"]
assert isinstance(items, ArrayAttr)
count = len(items)
first = items.data[0].value.data
```

Arrays are iterable; their `data` field contains the attribute sequence.
For `IntegerAttr`, `value.data` is the integer value. Integer attributes in
this interface use `i64`. Standard Python containers, control flow, and
arithmetic are available. Return a JSON-compatible dictionary:

```python
return {"count": count, "values": [first]}
```

The driver loads the extension with the pinned xDSL interpreter, calls
`analyze`, and compares its JSON result to the task's expected result. Return
semantic rejection as the error dictionary specified by the task, rather than
aborting the process. Do not change the driver or fixtures.

## Native graph analysis

For `ana-fusion-match`, the registered `func` dialect represents real typed
calls, not a `study.request` dictionary. The `FuncOp` named `subject` has a
`layout` string attribute (`NCHW` or `NHWC`). Callee symbols include `conv2d`,
`bias_add`, and `relu`; other calls may also occur.

```python
from xdsl.dialects.func import CallOp, FuncOp
from xdsl.dialects.builtin import TensorType
subject = next(op for op in module.ops
               if isinstance(op, FuncOp) and op.sym_name.data == "subject")
calls = [op for op in subject.body.block.ops if isinstance(op, CallOp)]
callee = calls[0].callee.root_reference.data
producer = calls[0].arguments[0].owner
single_use = calls[0].res[0].has_one_use()
shape = calls[0].res[0].type.get_shape()
```

Check `isinstance(producer, CallOp)` before inspecting a defining call; block
arguments have a block owner. Tensor types expose `get_shape()` as an integer
tuple. Report each match as indices into the subject's call sequence,
excluding its return. Uses include every actual operand occurrence and returns.

## Graph manifest emission

For `emit-graph-manifest`, use `subject.body.block.args` for ordered inputs and
`subject.body.block.ops` for operation order. `CallOp.arguments` and `CallOp.res`
contain operands and results; `ReturnOp.arguments` contains ordered returns.
SSA values can be dictionary keys and distinguish different results of one
operation. User attributes are in `op.attributes`; exclude a `callee` field
if present. Return JSON-compatible Python objects.

`TensorType.element_type` prints the element type with `str(...)`; `get_shape()`
returns dimensions, with negative values denoting dynamic extents. Rank-zero
tensors have an empty shape. `StringAttr.data`, `FloatAttr.value.data`, and
`IntegerAttr.value.data` expose scalar attributes. Integer attributes with
`value.type.width.data == 1` are Booleans. `ArrayAttr` is iterable. Sort user
attribute names when constructing the contract's attribute-pair array.

## Native rewrite entry point

For rewrite tasks implement `transform(module)` and mutate the supplied module.
The driver verifies and prints it; a separate process reparses the result and
observes the SSA graph. A returned dictionary is not a rewrite.

`rew-add-zero` supplies a `FuncOp` named `subject` containing tensor-typed
`CallOp` instances named `splat` and `add`. This small tensor dialect has the
same semantics in all systems. Splat has scalar `value` metadata (`IntegerAttr`
or `FloatAttr`); add has Boolean `no_signed_zeros` metadata (an i1 IntegerAttr).
There is no request map. Calls use private declarations and native SSA values.

Access `subject.body.block.ops`, `op.arguments`, `op.res`, and
`op.callee.root_reference.data`; a result's `owner` identifies its definition.
Tensor types expose `element_type` and `get_shape()`. A result supports
`replace_all_uses_with(value)`; its block supports `erase_op(op)`. Snapshot
`list(block.ops)` when erasing. `value.uses` includes return uses. Attribute
payloads are at `attr.value.data`; `math.copysign(1, value)` distinguishes
floating negative zero. Final checks cover structure, types, constant liveness,
numerical outputs, and strict signed-zero behavior.

`rew-redundant-cast` uses the same rewrite entry with unary `cast_A_B`
calls, such as `cast_i8_i16`. Tensor source/result types describe element types
and shape; there is no request map. Compare full types before replacing a
result. `type.get_shape()` compares shapes, `str(type.element_type)` yields
the element spelling, and `IntegerType.width.data` gives integer width. Preserve
an inner cast when its result has other uses. Scoring includes integer
wraparound, non-integral floating inputs, precision loss, and signed-zero bits.

For `rew-transpose-pair`, unary callees begin with `transpose_`; the suffix
encodes element type and source/result shapes. `op.attributes["perm"]` is an
`ArrayAttr` of `IntegerAttr` entries. Validate indices and tensor result
dimensions before rewriting, including rank-zero lists. Preserve shared inner
results. Invalid permutations must raise `ValueError("invalid-permutation")`
before the driver prints any transformed IR.

`con-instruction-select` uses the same mutation entry. Both `matmul` and the
matching target `mma_m16n16k16` are declared. Inspect ranked operand/result
types; assign `op.properties["callee"] = SymbolRefAttr(name)` to retarget.
Use `ArrayAttr` of `IntegerAttr(value, i64)` for `tiles` metadata. Preserve other
keys and every result user; negative or zero dimensions cannot select the target.
