# Transform

`transform@2.1.0` is the installable owner for reusable explicitly staged Fn
and Mod transformations. `pass` is an ordinary fn name, not a declaration
kind, keyword, pattern object, or pipeline class.

## Implemented operations

```joggle
fn pass(input: fn, before: fn, after: fn) -> fn;
fn inline(input: fn) -> fn;
fn inline(input: mod) -> mod;
fn resolve(input: mod) -> mod;
```

`pass` applies one concrete typed equation. Its two equation arguments are
ordinary compiler-domain lambdas:

```joggle
fn reorder(input: fn) -> fn {
  return @tr.pass(
    input,
    (x: word) -> word => consume(produce(x)),
    (x: word) -> word => produce(consume(x))
  );
}
```

The left lambda's arguments are pattern variables. Calls match by declaration
identity, specialized compiler bindings, exact Types, and dataflow structure;
no string or operator-name dispatch participates. The right lambda has the
same typed signature and is cloned at each non-overlapping match. The pass
recurses through existing callable bodies, rewires closure captures, preserves
the replaced root location, and removes dead pure producers.

Equations are currently deliberately strict: one returned value, one block,
concrete argument Types, and no effect-typed or zero-result calls. This is a
sound usable base, not yet the final polymorphic rule system. Generic typed
lambda parameters and richer multi-result equations remain implementation
gates.

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
  return @fuse_f32x4(input);
}
```

These are library fns, not language keywords. Dead-expression cleanup is part
of the equation transaction. Every transformation uses Types, def-use,
dominance, closure captures, and effect tokens for legality. Tensor libraries
state algebraic laws in terms of overloaded `map`, `[]`, ordered `reduce`, and
higher-order composition; they do not match Conv, Relu, GEMM, or imported
operator names.
