# Staging

Joggle uses ordinary typed fns for compiler work. Importing a model,
transforming IR, inspecting it, and writing output do not require separate pass
or backend classes.

```joggle
fn read(input: bytes) -> mod;
fn optimize(input: mod, policy: type) -> mod;
fn inspect(input: mod) -> bytes;

fn prepare(input: bytes, policy: type) -> mod {
  model = @read(input);
  return @optimize(model, policy);
}
```

The names are conventional, not reserved. A library may expose small
fns and a convenient composed pipeline. Users can call either.

## Call semantics

`@call(...)` requests compiler-time evaluation. The selected fn may have
a source body or a native C++ binding. Failure is diagnosed at the call site.
An ordinary call denotes computation that remains in the program.

This distinction is enforced during materialization. Known operands never
cause an ordinary program call to invoke a host binding.

## Native binding

Host-only work such as parsing ONNX or writing an object file is declared in
source and implemented through `Compiler::bind`:

```cpp
compiler.bind(mod, "optimize",
              [](joggle::Mod input, const joggle::Type& policy,
                 joggle::Diag& diagnostics)
                  -> std::optional<joggle::Mod> {
                return optimize(std::move(input), policy, diagnostics);
              });
```

The C++ signature selects a source overload. Joggle checks argument and result
domains before invocation. The returned mod is published only on success.

## Transformation substrate

Today a native compiler fn can materialize a `Fn`, open a
transactional edit, inspect exact callees and typed operands, change calls,
and commit after verification. This is the low-level substrate for constant
folding and user-defined structural transformations.

An explicit call may pass a typed lambda to a `fn` parameter. The lambda
is materialized as a verified anonymous `Fn`, passed directly to the
compiler fn, and may be returned for a later `@` call. It is not encoded
as scalar metadata. Since the compiler-domain `fn` accepts any signature,
such a lambda can state its result explicitly:

```joggle
@transform.replace(
  model,
  (x: tensor) -> tensor => relu(conv(x)),
  (x: tensor) -> tensor => conv_relu(x)
)
```

This is ordinary bidirectional typing: the result annotation supplies the
expected type for nested generic calls and is checked against the body. It
does not create a pattern language, a host callback, or a new transformation
kind.

Transformations compose with the language's existing fn call and local
binding rules. There is no separate sequence object:

```joggle
fn optimize(input: mod) -> mod {
  normalized = @canonicalize(input);
  return @transform.resolve(normalized);
}
```

Here `input` and `normalized` are ordinary compiler-domain `mod` values.
Typed lambdas use the same materializer whether they appear inside a residual
higher-order call or as an argument to an explicitly staged compiler fn.
The compiler does not convert them through a pattern, strategy, or scalar
metadata representation.

The shipped `transform` Mod binds this declaration to the
equivalence-checking C++ `joggle::replace(Compiler&, ...)` overload with
`Compiler::bind`. Eligible source bodies are expanded into a bounded canonical
expression encoding; bodyless calls remain exact-identity leaves. A mismatch
is diagnosed before
the existing atomic structural replacement opens an edit. `replace` is not
reserved syntax, and the lambdas remain verified Fns over the same IR
rather than a second rewrite declaration or pattern representation.

## Source resolution

`transform.resolve` recursively instantiates reachable source-defined calls.
Bodyless calls remain explicit leaves. A later compiler or emitter consumes the
complete Mod once and rejects unsupported leaves; resolution never executes
them through native bindings. The C++ primitive is `Compiler::resolve`.

## Reproducibility

Compiler fns should return new values instead of mutating shared mod
state. Mod snapshots are copy-on-write, fn edits are transactional,
and binary payloads are content-addressed. Native bindings that may execute
under residual control must opt into `HostEval::Hermetic` only when they
are deterministic and free of observable host effects.

## Command-line boundary

`joggle run mod.joggle fn input -o output` supports portable
`bytes -> bytes`, `bytes -> mod`, `mod -> mod`, and
`mod -> bytes` entry points. This boundary is for composing tools; it does
not create a special compiler-fn kind.
