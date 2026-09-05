# Transform

`transform@4.0.0` owns reusable explicitly staged transformations:

```joggle
pub fn pass(input: fn, laws: mod) -> fn;
pub fn inline(input: fn) -> fn;
pub fn inline(input: mod) -> mod;
pub fn resolve(input: mod) -> mod;
```

These are ordinary fns, not language keywords, pass classes, or a second IR.
Users compose them with normal source code and mark compiler execution with
`@`.

`pass` interprets a pure, bodyful, two-result fn as an oriented equation. The
first returned expression is matched and the second is cloned as its
replacement:

```joggle
mod laws@1.0.0 {
  import algebra@1 as a;

  fn commute(value: a.word) -> (a.word, a.word) {
    return a.other(a.keep(value)), a.keep(a.other(value));
  }
}
```

Arguments and generics are pattern variables. Matching uses declaration
identity, exact Types, Known callee bindings, and dataflow structure—not names
or strings. Effects, unbound replacement values, zero-result calls, and control
flow reject an equation. The pass descends into existing callable bodies and
rewires explicit captures transactionally.

`inline` expands source-defined calls and anonymous callable bodies, including
multi-block CFGs. Callee returns converge through typed continuation block
arguments rather than a special region result. `resolve` materializes reachable
source definitions into a closed Mod while leaving bodyless implementation
boundaries visible. Neither operation executes Residual program calls through
host callbacks.

Domain-specific structural transforms remain with the domain that defines
their accepted form: `tensor.fuse` and `tensor.loops` therefore belong to
Tensor. Algebraic identities that are meaningful across domains remain
ordinary user-selected law Mods consumed by `transform.pass`.
