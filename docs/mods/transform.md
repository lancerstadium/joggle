# Transform

`transform` is the installable owner for reusable explicitly staged Fn and Mod
transformations. It adds no `pass`, pattern, rewrite, pipeline, or result
declaration to the language.

## Stable operation

```joggle
fn resolve(input: mod) -> mod;
```

`resolve` materializes every reachable source-defined call at its concrete
types and compiler bindings, deduplicates identical instances, and leaves
bodyless calls visible. It never invokes Residual leaves through host
callbacks. A later whole-Mod consumer must accept or reject that explicit leaf
set.

## Transformation direction

Direct transformation works on the actual `Fn`: calls, values, blocks, and
nested callable bodies. The source-facing API is intentionally not declared
until its first implementation exists. The intended composition remains
ordinary Joggle code:

```joggle
fn optimize(input: fn) -> fn {
  input = @inline(input);
  input = @fuse(input);
  return @dce(input);
}
```

The names above are planned library fns, not language keywords. They must edit
concrete structure transactionally and use Types, def-use, dominance, closure
captures, and effect tokens for legality. They may not match Conv, Relu, GEMM,
or any other operation name.

The old `replace(input, before, after)` expression-template API is not part of
the accepted design and remains scheduled for source and C++ removal. It must
not be used for new code or presented as generic fusion.
