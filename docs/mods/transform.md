# Transform

`transform@3.0.0` owns reusable explicitly staged transformations:

```joggle
fn pass(input: fn, laws: mod) -> fn;
fn inline(input: fn) -> fn;
fn inline(input: mod) -> mod;
fn resolve(input: mod) -> mod;
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

`inline` expands source-defined single-block calls and anonymous callable
bodies. `resolve` materializes reachable source definitions into a closed Mod
while leaving bodyless implementation boundaries visible. Neither operation
executes Residual program calls through host callbacks.

Tensor fusion laws will be added only after indexed construction, access, and
loop analyses are exercised by real NN bodies. They belong in a user-selected
laws Mod, not in Tensor, a magic optimization package, or compiler C++.
