# Transform

`transform@1.2.0` is the installable owner for reusable explicitly staged Fn
and Mod transformations. It adds no `pass`, pattern, rewrite, pipeline, or
result declaration to the language.

## Implemented operations

```joggle
fn inline(input: fn) -> fn;
fn resolve(input: mod) -> mod;
```

`inline` replaces every source-defined or anonymous single-block Call visible
in its input snapshot with the callee's actual operations. Runtime arguments,
Known fn bindings, result edges, locations, nested callable values, and closure
captures are remapped into the caller. Opaque, dynamic, and multi-block calls
remain visible. Newly cloned calls wait for a later invocation, keeping one
call deterministic and bounded.

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
  input = @fuse(input);
  return @dce(input);
}
```

These are library fns, not language keywords. `inline` is implemented; `fuse`
and `dce` in the example remain planned. Every transformation must edit
concrete structure transactionally and use Types, def-use, dominance, closure
captures, and effect tokens for legality. It may not match Conv, Relu, GEMM,
or any other operation name.

The old `replace(input, before, after)` expression-template API is not part of
the accepted design and remains scheduled for source and C++ removal. It must
not be used for new code or presented as generic fusion.
