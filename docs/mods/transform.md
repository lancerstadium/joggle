# Transform

`transform@1.1.0` exposes typed semantic replacement and source resolution
through the same ordinary
Mod/fn mechanism as every other compiler extension. It adds no source
keyword, pattern declaration, pass registry, or result wrapper.

## Public fns

```joggle
fn replace(
  input: fn,
  before: fn,
  after: fn
) -> fn;

fn replace(
  input: mod,
  before: fn,
  after: fn
) -> mod;

fn resolve(input: mod) -> mod;
```

Both `before` and `after` are normal typed Fn values. Capture-free typed
lambdas are the concise source form:

```joggle
mod factoring@1.0.0 {
  import transform@1;

  type value();
  fn step(input: value) -> value;

  fn twice(input: value) -> value {
    return step(step(input));
  }

  fn run(input: fn) -> fn {
    return @transform.replace(
      input,
      (x: value) => step(step(x)),
      (x: value) => twice(x)
    );
  }
}
```

Because `twice` expands to the first expression, this example demonstrates
checked fn factoring. It does not claim an optimized kernel.

`@` is the only staging marker. Without it, `transform.replace` is an ordinary
residual call rather than an implicitly executed compiler action.

## Correctness contract

The two Fns must have identical typed signatures and be pure rooted
expressions. Matching compares exact call declarations, Known values, callable
references, result positions, and repeated-hole SSA equality. Shared pure DAG
ancestors are preserved until their last external use disappears.

Before editing, the compiler recursively expands eligible source-bodied calls
under an explicit bound and requires exact definitional equivalence. Opaque
calls remain exact-identity leaves. A mismatch, recursion, effect token,
malformed expression, or failed edit publishes nothing. Zero matches are a
successful no-op.

The Fn overload commits all maximal non-overlapping matches in one
transaction. The Mod overload works on a private Mod copy and publishes
only when every materialized member succeeds. No partially transformed Mod
is observable.

`resolve` performs a different whole-program operation. It instantiates every
reachable source-defined call at its concrete types and compiler parameters,
deduplicates identical instances, and leaves bodyless calls visible. It does
not execute those leaves. A later emitter consumes the resolved Mod once and
must reject unsupported leaves.

## Boundary

Definitional equivalence is intentionally conservative. It does not prove
algebraic reassociation, approximate floating-point identities, or empirical
correctness. A future proof provider may be another explicitly staged
fn, but it must not silently weaken this Mod's contract.
