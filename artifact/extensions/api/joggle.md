# Joggle analysis extension interface

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
