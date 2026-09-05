# Design 0009: Implementation closure

Status: accepted

## Problem

A Residual `Fn` is a program, not a host callback graph. Executing each `Op`
by looking up a C++ binding would require one binding for every arithmetic,
memory, tensor, and custom operation. It would also hide the implementation
boundary from transformations and prevent whole-function code generation.

Joggle instead needs a complete call graph that a compiler fn can inspect and
emit once. This must preserve the single Mod/type/fn extension model without
introducing a target, backend, kernel, or pass class.

## Staging boundary

An ordinary call is Residual and remains in `Fn` IR. An explicit `@` call is
evaluated by the compiler. A native binding may implement such a compiler-time
service, but it is never the execution semantics of an ordinary Residual call.

Typical native services operate at coarse boundaries:

- decode an external model into a `Mod`;
- transform or analyze a complete `Fn` or `Mod`;
- emit a resolved `Mod` into bytes; or
- measure a complete compiled candidate.

The number of calls in the input program does not change the number of host
bindings required by an extension.

## Source resolution

`resolve(input: mod) -> mod` recursively materializes every reachable
source-defined call at its concrete types and compiler parameters. Each unique
instance is inserted once into the returned Mod and calls are redirected to
that instance. The input snapshot is unchanged.

Bodyless fns remain explicit leaves. Resolution neither invokes them nor
claims that they are supported by a machine. Recursion, an invalid instance,
or an inconsistent call fails the whole transaction.

This produces an implementation closure:

1. every reachable bodyful call names a concrete Fn in the returned Mod; and
2. every remaining unresolved call is visibly bodyless.

A later emitter consumes the complete closure once. It must reject any leaf it
does not support. Support is therefore a property of that ordinary compiler fn,
not a registry in compiler core.

## Transformation surface

Resolution is itself an ordinary staged fn:

```joggle
fn prepare(input: mod) -> mod {
  optimized = @optimize(input);
  return @transform.resolve(optimized);
}

fn compile(input: mod) -> bytes {
  return @emit(@prepare(input));
}
```

`optimize`, `resolve`, and `emit` are normal module members. Their composition
does not require a Pass, Pipeline, Target, Artifact, or Result object.

Typed lambdas already provide the source-level way to write individual
replacement passes. Users compose those pass fns with ordinary source calls;
they do not bind each pass in C++:

```joggle
fn pass(input: mod, before: fn, after: fn) -> mod {
  return @transform.replace(input, before, after);
}

fn optimize(
  input: mod,
  first_before: fn,
  first_after: fn,
  second_before: fn,
  second_after: fn
) -> mod {
  first = @pass(input, first_before, first_after);
  return @pass(first, second_before, second_after);
}
```

`transform.replace` is one generic transactional service. `pass` and
`optimize` are source-defined and need no native entries.

## Implementation selection

A semantic fn can be implemented in either of two visible ways:

1. it has a transparent source body that resolution can instantiate; or
2. an explicitly staged transformation replaces its call with a different,
   typed implementation fn before resolution.

There is no hidden second body attached to a declaration. Structural and
effect verification applies to every replacement. Definitional equivalence is
useful for transparent factoring, but it is not falsely presented as a proof
of arbitrary optimized kernels; stronger validation remains an explicit
compiler fn.

## Consequences

- `Compiler::run(const Fn&, ...)` is not part of the API.
- Fixed-width Residual values are not host execution values.
- `Compiler::bind` remains for explicitly staged host services.
- Fn resolution is deterministic, snapshot-local, and content-addressable.
- A truly new machine primitive eventually needs emitter support once in its
  implementation Mod; model authors never bind each occurrence.

## Rejected designs

- Interpreting every Residual `Op` through a native binding.
- Treating a successful structural test as compiled kernel execution.
- A fixed lowering ladder or privileged backend registry.
- A hidden implementation selected by an operation name.
- Inlining all bodies into one expression and losing the call graph.
- Accepting an unsupported bodyless leaf silently.

## Next gates

1. [x] Remove direct Fn host execution and fixed-width scalar bindings.
2. [x] Resolve concrete source instances without executing bodyless leaves.
3. [x] Expose resolution through the ordinary `transform` Mod.
4. [x] Compose typed-lambda replacement passes as ordinary source fns.
5. [ ] Resolve a transformed real ONNX model to an auditable leaf set.
6. [ ] Add one whole-Mod emitter and compare its output numerically with a
   trusted runtime before claiming executable kernel support.
