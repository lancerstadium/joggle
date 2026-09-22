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

The test driver creates a function named `subject` with a `request` metadata
dictionary. Read that native IR attribute with:

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
