# Transform module

`transform@1.0.0` exposes typed semantic replacement through the same ordinary
Module/function mechanism as every other compiler extension. It adds no source
keyword, pattern declaration, pass registry, or result wrapper.

## Public functions

```joggle
fn replace(
  input: function,
  before: function,
  after: function
) -> function;

fn replace(
  input: module,
  before: function,
  after: function
) -> module;
```

Both `before` and `after` are normal typed Function values. Capture-free typed
lambdas are the concise source form:

```joggle
module optimize@1.0.0 {
  import transform@1;

  fn run(input: function) -> function {
    return @transform.replace(
      input,
      (x: tensor, w: tensor) => relu(conv(x, w)),
      (x: tensor, w: tensor) => conv_relu(x, w)
    );
  }
}
```

`@` is the only staging marker. Without it, `transform.replace` is an ordinary
residual call rather than an implicitly executed compiler action.

## Correctness contract

The two Functions must have identical typed signatures and be pure rooted
expressions. Matching compares exact call declarations, Known values, callable
references, result positions, and repeated-hole SSA equality. Shared pure DAG
ancestors are preserved until their last external use disappears.

Before editing, the compiler recursively expands eligible source-bodied calls
under an explicit bound and requires exact definitional equivalence. Opaque
calls remain exact-identity leaves. A mismatch, recursion, effect token,
malformed expression, or failed edit publishes nothing. Zero matches are a
successful no-op.

The Function overload commits all maximal non-overlapping matches in one
transaction. The Module overload works on a private Module copy and publishes
only when every materialized member succeeds. No partially transformed Module
is observable.

## Relation to outlining

`transform.replace` is the direct user surface when concrete typed lambdas can
state the before and after expressions. Transparent composites such as
`qdq.nchw_conv` span many tensor shapes, so their native selector instead uses
the C++ `joggle::outline` helper. That helper instantiates the ordinary
reference body for each selected concrete call and then uses the same matcher,
equivalence relation, shared-DAG handling, and transaction machinery.

The distinction is about how a concrete expression is supplied, not two kinds
of transformation or IR.

## Boundary

Definitional equivalence is intentionally conservative. It does not prove
algebraic reassociation, approximate floating-point identities, or empirical
correctness. A future proof provider may be another explicitly staged
function, but it must not silently weaken this Module's contract.
