# Joggle extension interface

## Native rewrite entry point

For rewrite tasks, use `fn transform(m: Mod) -> bool`, returning whether the
mod changed. The driver runs this entry, reparses its printed IR, and queries
the resulting graph in a separate process. Returning a report is not a rewrite.

`rew-add-zero` supplies a `subject` function with typed tensor SSA calls to
`splat()` and `add(a, b)`. `splat` has a scalar `value` metadata attribute;
`add` has Boolean `no_signed_zeros` metadata. These are the same small tensor
dialect and operation semantics in all three systems. There is no `request`
attribute. Input and output types may differ because `add` broadcasts.

Use `ir.def(value)`, `ir.args(op)`, `ir.outs(op)`, `ir.type(value)`, and
`ir.meta(op)` to inspect the pattern. Tensor element types are
`args(ir.type(value))[0]`. `ir.replace(m, old_value, new_value)` redirects SSA
uses; `ir.erase(m, op)` removes an unused operation. `ir.users(value)` includes
returns. Iterate a snapshot of `ir.ops(ir.find(m, "subject"), ["call"])` and
check `ir.live` before inspecting handles that a prior rewrite may have erased.
`base.real(attr)` reads a numeric attribute; `base.text(attr)` preserves the
minus sign of floating negative zero. The oracle checks the final graph,
constant liveness, result types, numerical outputs, and signed-zero behavior.

For `rew-redundant-cast`, the same rewrite entry receives typed unary
`cast_A_B` calls, where A and B are element-type spellings such as `i8` and
`i16`. Source and result tensor types carry the shapes. Casts have no request
metadata. Inspect an inner conversion with `ir.def(ir.args(op)[0])`; compare
full types before replacing values and compare `args(type)[1]` to check shape.
The oracle preserves shared inner results, signed-zero bits, and non-integral
floating inputs. Narrowing, mixed float/integer, and shape-changing cases must
retain their operations.

For `rew-transpose-pair`, unary callees have names beginning `transpose_`;
the suffix encodes element type and source/result shapes. Read the `perm`
list from operation metadata, and tensor dimensions with `args(args(type)[1])`.
Check permutation entries with `kind` and `int`, including rank-zero lists.
Preserve an inner result with other users. Invalid permutations must fail with
`assert(false, "invalid-permutation")` before emitting any transformed IR.

## Analysis entry point

Edit the supplied `module.jog`. Its public entry point is:

```jog
mod extension
use base
use ir

fn analyze(m: Mod) -> dict {
  return {}
}
```

For attribute-request tasks, the test driver creates a function named `subject`
with a `request` metadata dictionary. Read that native IR attribute with:

```jog
let request = ir.meta(ir.find(m, "subject"), "request")
let items = base.get(request, "items")
let count = len(items)
let first = base.int(items[0])
```

`get` and indexed attribute access return `Attr`; `base.int` converts an integer
attribute to `int`. Use `len` and indexing for arrays. Functions, conditionals,
and loops use braces. `let` declares a value; `var` declares a mutable value.
Lists support concatenation with `+`. Dictionary keys are strings:

```jog
var result: dict = {}
result["count"] = count
return result
```

The driver runs `joggle query extension.analyze` and compares the returned
dictionary to the task's expected result. Return semantic rejection as the
error dictionary specified by the task, rather than aborting the process.
Do not change the driver or fixtures.

## Native graph analysis

For `ana-fusion-match`, `subject` contains real typed calls, not a `request`
dictionary. Its `layout` metadata is `NCHW` or `NHWC`. The graph uses declarations
named `conv2d`, `bias_add`, and `relu`; other calls may also occur.

```jog
let subject = ir.find(m, "subject")
let calls = ir.ops(subject, ["call"])
let operands = ir.args(calls[0])
let producer = ir.def(operands[0])
let consumers = ir.users(ir.outs(calls[0])[0])
let result_type = ir.type(ir.outs(calls[0])[0])
let dimensions = args(args(result_type)[1])
```

Check `ir.live(producer)` before inspecting a definition; function arguments
have no defining operation. Tensor types are `tensor<f32, [d0, ...]>`.
Report each match as indices into the subject's call sequence, excluding its
return operation. Uses include every actual operand occurrence and returns.

## Graph manifest emission

`emit-graph-manifest` also supplies a native `subject` function. Its inputs are
`ir.params(subject)`; traverse `ir.ops(subject)` in structural order. Calls have
kind `"call"`, results from `ir.outs(op)`, and user attributes from `ir.meta(op)`.
The return has kind `"return"` and ordered operands from `ir.args(op)`.
`ir.key(value)` distinguishes values within this mod, including distinct
results of one call. Use native value identity, not source variable names.

For tensor types, `args(ir.type(value))[0]` is the element type and
`args(args(ir.type(value))[1])` gives dimension types. `text(type)` prints a
type; `name(dimension) == "_"` denotes a dynamic extent. `int(dimension)` reads
a concrete integer dimension. Metadata `keys` are lexicographically ordered;
`base.get(metadata, key)` preserves its attribute value. Build computed JSON
objects using a mutable `dict` and indexed assignments, as above; dictionary
literals contain attribute literals, not arbitrary expressions.
