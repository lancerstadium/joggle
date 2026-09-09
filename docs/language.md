# Language

`.jog` is Joggle's only source and readable IR format. It describes reusable
modules, functions, types, control flow, and explicit compile-time workflows.
It is intentionally not a second pipeline or kernel language.

The implemented M1 surface is conventional:

```jog
module demo
use tensor

fn matmul<T, M, N, K>(
  a: tensor<T, [M, K]>,
  b: tensor<T, [K, N]>
) -> tensor<T, [M, N]> {
  var c = tensor<T, [M, N]>(0)
  for i in 0..M, j in 0..N {
    var sum = T(0)
    for k in 0..K {
      sum += a[i, k] * b[k, j]
    }
    c[i, j] = sum
  }
  return c
}
```

The language uses ordinary `module`, `use`, `fn`, `let`, `var`, `for`, `if`,
and `return`. It has generics, structural types, attributes, and overloadable
operators. It has no `graph`, `kernel`, `compute`, `map`, `fold`, `rewrite`,
`region`, or `pass` syntax.

Multiple loop variables denote a lexically nested Cartesian product. `return`
is always an ordinary statement in the function block. Mutable values crossing
a `for` or `if` boundary become block arguments, results, and an internal
`yield`; these mechanics remain visible to C++ transforms but are recovered as
normal source syntax by the printer.

Calls, literals, indexing, unary operators, and common binary operators are
implemented. Operators normalize to ordinary function calls such as
`operator +` and `operator []`; adding a concrete overload does not add a new
IR operation kind.

`let` bindings are immutable. `var` bindings may be reassigned with `=` or
`+=`, and tensor-like values may use `value[i, j] = next`. The latter normalizes
to a call of `operator []=` returning the updated value, so mutation remains an
explicit value flow.

Canonical printing deliberately discards comments and incidental whitespace.
Printing and reparsing must produce a structurally equal module.

### Next language slice

`@build(...)` will enter compile-time evaluation in M2. Calls inside the
selected function will remain ordinary calls and will not repeat `@`. Function
attributes beyond `[host]`, user type declarations, typed overload resolution,
and the minimal reflection library also remain for later slices. They will
extend this one language rather than introduce pipeline or kernel syntax.
