# Language

`.jog` is Joggle's only source and readable IR format. It describes reusable
modules, functions, types, control flow, and explicit compile-time workflows.
It is intentionally not a second pipeline or kernel language.

The planned surface is conventional:

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

`@build(...)` at module scope enters compile-time evaluation. Calls inside the
selected function remain ordinary calls; they do not repeat `@`. The evaluator
will initially support only the values and control flow needed to compose and
write transforms.

The current bootstrap accepts and canonically prints a module declaration.
The rest of this document is the contract for M1, not a claim that every form
is implemented yet.
