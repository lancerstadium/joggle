# Compiler functions

Joggle uses ordinary typed functions for compiler work. Importing a model,
transforming IR, inspecting it, and writing output do not require separate pass
or backend classes.

```joggle
fn read(input: bytes) -> module;
fn optimize(input: module, policy: type) -> module;
fn inspect(input: module) -> bytes;

fn prepare(input: bytes, policy: type) -> module {
  model = @read(input);
  return @optimize(model, policy);
}
```

The names are conventional, not reserved. A library may expose small
functions and a convenient composed pipeline. Users can call either.

## Staging

`@call(...)` requests compiler-time evaluation. The selected function may have
a source body or a native C++ binding. Failure is diagnosed at the call site.
An ordinary call denotes computation that remains in the program.

This distinction is enforced during materialization. Known operands never
cause an ordinary program call to invoke a host binding.

## Native binding

Host-only work such as parsing ONNX or writing an object file is declared in
source and implemented through `Compiler::bind`:

```cpp
compiler.bind(module, "optimize",
              [](joggle::Module input, const joggle::Type& policy,
                 joggle::Diagnostics& diagnostics)
                  -> std::optional<joggle::Module> {
                return optimize(std::move(input), policy, diagnostics);
              });
```

The C++ signature selects a source overload. Joggle checks argument and result
domains before invocation. The returned module is published only on success.

## Transformation substrate

Today a native compiler function can materialize a `Function`, open a
transactional edit, inspect exact callees and typed operands, change calls,
and commit after verification. This is the low-level substrate for fusion,
constant folding, layout changes, and tensor-storage optimization.

An explicit call may pass a typed lambda to a `function` parameter. The lambda
is materialized as a verified anonymous `Function`, passed directly to the
compiler function, and may be returned for a later `@` call. It is not encoded
as scalar metadata.

Typed replacement is the same ordinary-function mechanism:

```joggle
import transform@1;

optimized = @transform.replace(
  input,
  (x: tensor, w: tensor) => relu(conv2d(x, w)),
  (x: tensor, w: tensor) => conv_relu(x, w)
);
```

Transformations compose with the language's existing function call and local
binding rules. There is no separate sequence object:

```joggle
fn fuse(input: function) -> function {
  return @transform.replace(input, before, after);
}

fn optimize(input: function) -> function {
  fused = @fuse(input);
  return @canonicalize(fused);
}
```

Here `input`, `before`, `after`, and `fused` are ordinary compiler-domain
`function` values. Typed lambdas use the same materializer whether they appear
inside a residual higher-order call or as an argument to an explicitly staged
compiler function. The compiler does not convert them through a pattern,
strategy, or scalar metadata representation.

The shipped `transform` Module binds this declaration to the
equivalence-checking C++
`joggle::replace(Compiler&, ...)` overload with `Compiler::bind`. Eligible
source bodies are expanded into a bounded canonical expression encoding;
bodyless calls remain exact-identity leaves. A mismatch is diagnosed before
the existing atomic structural replacement opens an edit. `replace` is not
reserved syntax, and the lambdas remain verified Functions over the same IR
rather than a second rewrite declaration or pattern representation.

For a generic reference-bodied operation, native code may use
`joggle::outline`. Its selector returns the concrete arguments of that normal
function for one eligible root call. The core then instantiates the reference
body, checks the selector's exact SSA bindings, proves equivalence, preserves
shared ancestors, repeats to a structural bound, and publishes once. This
keeps operator-specific legality in its Module while removing duplicated
template construction, matching, proof, sweep, and Module-update code.

## Recursive specialization

`Compiler::specialize` expands source-defined calls until a caller-provided
predicate accepts every remaining declaration. It is useful when an extension
wants to define a primitive boundary without a global lowering registry.

The predicate receives exact `FunctionDecl` handles. Accepted calls remain;
other calls with materializable bodies are specialized recursively. Opaque
unaccepted calls and recursive expansion fail with diagnostics.

## Reproducibility

Compiler functions should return new values instead of mutating shared module
state. Module snapshots are copy-on-write, function edits are transactional,
and binary payloads are content-addressed. Native bindings that may execute
under residual control must opt into `HostEvaluation::Hermetic` only when they
are deterministic and free of observable host effects.

## Command-line boundary

`joggle run module.joggle function input -o output` supports portable
`bytes -> bytes`, `bytes -> module`, `module -> module`, and
`module -> bytes` entry points. This boundary is for composing tools; it does
not create a special compiler-function kind.
