# RFC 0007: Reference-bodied transformations

Status: implementation gates 1--7 complete

## Purpose

Joggle now has typed Function values, atomic expression replacement, a tensor
Module, and a real ONNX path. The next gate is not a schedule object or a fixed
kernel IR. It is a correctness boundary for user-defined optimized functions.

Today `replace` proves type, data-flow, ownership, and effect safety. It cannot
prove that two pure expressions compute the same value. Consequently, replacing
an expression by a bodyless call is structurally safe but semantically only an
assertion by the extension author.

## Accepted source model

An optimized function carries its reference semantics as its ordinary source
body:

```joggle
fn conv_then_relu(
  input: tensor<f32, [1, 16, 32, 32]>,
  weight: tensor<f32, [32, 16, 3, 3]>,
  bias: tensor<f32, [32]>
) -> tensor<f32, [1, 32, 30, 30]> {
  return relu(conv(input, weight, bias, [1, 1], [0, 0, 0, 0],
                   [1, 1], 1));
}
```

This is one normal `fn` body and therefore a transparent composite, not a fused
kernel implementation. Concrete realization later needs an executable body
whose structure differs for a justified reason; it must not attach a second
hidden meaning to this declaration.

A transformation remains an ordinary explicitly staged function:

```joggle
fn factor_pair(input: function) -> function {
  return @transform.replace(
    input,
    (x: tensor<f32, [1, 16, 32, 32]>,
     w: tensor<f32, [32, 16, 3, 3]>,
     b: tensor<f32, [32]>) =>
      relu(conv(x, w, b, [1, 1], [0, 0, 0, 0], [1, 1], 1)),
    (x: tensor<f32, [1, 16, 32, 32]>,
     w: tensor<f32, [32, 16, 3, 3]>,
     b: tensor<f32, [32]>) => conv_then_relu(x, w, b)
  );
}
```

`replace` is a library name, not syntax or a declaration category. Its native
implementation calls the equivalence-checking overload of the C++ replacement
primitive.

## Equivalence contract

The first implementation is deliberately decidable and conservative:

1. validate both lambdas using the existing expression-template rules;
2. recursively expand eligible source-defined expression calls;
3. canonicalize the resulting pure typed expression DAGs;
4. compare exact overload identities, generic properties, constants, argument
   positions, and normalized structure; and
5. run the existing effect-safe atomic replacement only when the normalized
   expressions are identical.

Expansion is bounded and detects recursion. Opaque calls are leaves and must
match by exact declaration identity. Algebraic identities, floating-point
reassociation, SMT reasoning, and empirical testing are outside this first
proof relation. Failure produces a diagnostic and publishes no edit.

This relation establishes definitional equivalence, not general mathematical
equivalence. A future proof provider may be an ordinary compiler function, but
the core will not add `law`, `trait`, `verify`, or `require` declarations.

## Location and composition

Typed lambdas identify semantic shapes; users do not name loop handles,
anchors, blocks, or string cursors. Ordinary higher-order functions compose
transformations. Repetition, choice, traversal, and search are library
functions over `function` values rather than keywords or a `Schedule` class.

The implementation must preserve diagnostics explaining three distinct
outcomes: no match, an unsafe structural match, and an unproved semantic
replacement. It must not encode success/failure as a public `Result` wrapper.

## Formats and implementation choice

This RFC does not introduce a device model. A later format Module will use
ordinary parameterized `type` declarations for values and ordinary `fn`
declarations for conversions and computations. Its first obligation is a
portable reference meaning that participates in the same equivalence relation.

After correctness composition works, candidate generation may return ordinary
Function values. Measurement and selection remain separate explicitly staged
functions. A selected Function, its inputs' static properties, and any profile
record must be content-addressed so an edge deployment can lock one choice and
rebuild it deterministically. No runtime search is mandatory.

## Rejected designs

- A `Schedule`, `Kernel`, `Target`, or `Device` class creates a second extension
  hierarchy.
- A fixed list of `split`, `tile`, `tensorize`, and `pipeline` syntax freezes
  one hardware model into the language.
- Type equality alone does not prove a replacement correct.
- A bodyless optimized call plus a trusted name table hides semantics in the
  compiler.
- String locations, cursor objects, and anchors make transformations depend on
  incidental program shape.
- An implicit cost model couples correctness to hardware ranking.

## Implementation gates

1. [x] Add bounded source-body normalization for token-free expression
   Functions without adding a second IR.
2. [x] Add an equivalence query with stable mismatch diagnostics and tests for
   recursion, opaque leaves, properties, and overload identity.
3. [x] Compose equivalence checking with atomic expression replacement.
4. [x] Replace a bodyless declaration with a source-bodied transparent
   composite and prove the positive and negative cases without calling it an
   executable fusion.
5. [x] Expose the primitive through an ordinary module function and exercise it
   from source with `@`. The installable `transform@1.0.0` Module accepts typed
   lambda Functions directly and has Function and Module overloads.
6. [x] Compose transformations through ordinary `fn`/`@call` sequencing. No
   public `seq`, `Strategy`, or `Result` abstraction is required.
7. [x] Preserve shared pure DAG ancestors during replacement without adding a
   pattern graph or cloning values that still have external users.
8. [ ] Represent and execute a real kernel body before adding scheduling,
   physical formats, or measured variant selection.

No target hierarchy, code emitter, machine-capacity model, or scheduling DSL is
implemented before gates 1--5 establish this semantic boundary.
