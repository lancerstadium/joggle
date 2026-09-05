# Transform

`transform@3.0.0` is the installable owner for reusable explicitly staged Fn
and Mod transformations. `pass` is an ordinary fn name, not a declaration
kind, keyword, pattern object, or pipeline class.

## Implemented operations

```joggle
fn pass(input: fn, laws: mod) -> fn;
fn inline(input: fn) -> fn;
fn inline(input: mod) -> mod;
fn resolve(input: mod) -> mod;
```

`pass` applies the ordinary equation fns in a Mod package, in declaration
order. An equation has two results with the same Type expression. The first
returned expression is its left side and the second is its replacement:

```joggle
// laws.joggle
mod laws@1.0.0 {
  import tensor@4 as t;

  fn fuse<E, S: list<int>>(
    make: (t.coord<S>) -> E,
    body: (E) -> E
  ) -> (t.tensor<E, S>, t.tensor<E, S>) {
    return
      t.map(t.map(S, make), body),
      t.map(S, (p: t.coord<S>) -> E => body(make(p)));
  }
}
```

```joggle
// pipeline.joggle
mod pipeline@1.0.0 {
  import transform@3 as tr;
  import laws@1 as laws;

  fn optimize(input: fn) -> fn {
    return @tr.pass(input, laws);
  }
}
```

The equation's arguments are pattern variables shared by both expressions.
Generic Types are solved from the candidate result and from values
structurally corresponding to arguments in the source left expression. The
concrete equation then matches declaration identity, specialized compiler
bindings, exact Types, and dataflow structure; no string or operator-name
dispatch participates. The second expression is cloned at each non-overlapping
match. The pass recurses through existing callable bodies, rewires closure
captures, preserves the replaced root location, and removes dead pure
producers.

Equation packages may also contain declarations and ordinary helper fns; only
bodyful fns with two identical result Type expressions are equations. Equations
are deliberately pure and single-block. An effect anywhere in their signature
or body, a zero-result call, an unbound replacement argument, or control flow
is rejected. Generic Types may be recovered beneath a direct left return
expression. Law-local bindings, multi-result pattern calls, and CFG-shaped
left expressions are not yet accepted.

`inline` replaces every source-defined or anonymous single-block Call visible
in its input snapshot with the callee's actual operations. It first applies the
same operation recursively to callable bodies already present in that
snapshot. Runtime arguments, Known fn bindings, result edges, locations,
nested callable values, and closure captures are remapped into the caller.
Opaque, dynamic, and multi-block calls remain visible. Newly cloned calls wait
for a later invocation, keeping one call deterministic and bounded.

`resolve` materializes every reachable source-defined call at its concrete
types and compiler bindings, deduplicates identical instances, and leaves
bodyless calls visible. It never invokes Residual leaves through host
callbacks. A later whole-Mod consumer must accept or reject that explicit leaf
set.

## Transformation direction

Direct transformation works on the actual `Fn`: calls, values, blocks, and
nested callable bodies. Composition remains ordinary Joggle code:

```joggle
fn optimize(input: fn) -> fn {
  input = @inline(input);
  return @pass(input, laws);
}
```

These are library fns, not language keywords. Dead-expression cleanup is part
of the equation transaction. Every transformation uses Types, def-use,
dominance, closure captures, and effect tokens for legality. Tensor libraries
state algebraic laws in terms of overloaded `map`, `[]`, ordered `reduce`, and
higher-order composition; they do not match Conv, Relu, GEMM, or imported
operator names.
