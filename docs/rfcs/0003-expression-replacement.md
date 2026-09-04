# RFC 0003: Typed expression replacement

Status: implemented

This RFC defines structural replacement safety. RFC 0007 adds conservative
definitional-equivalence checking before this primitive is used by a normal
compiler-facing module.

## Purpose

Joggle needs a transformation surface suitable for fusion, canonicalization,
and hardware-specific instruction selection without exposing cursor, anchor,
region, pattern, or rewrite-object APIs. The transformation must reuse the
typed `Function` values completed by RFC 0002 and must reject mutations whose
data-flow or effect boundary is not preserved.

## Source surface

Replacement is an ordinary compiler function supplied by a module:

```joggle
fn replace(input: function, before: function, after: function) -> function;

optimized = @replace(
  input,
  (x: tensor, w: tensor, b: tensor) => relu(conv2d(x, w) + b),
  (x: tensor, w: tensor, b: tensor) => conv_bias_relu(x, w, b),
);
```

`replace` is not a keyword. The two lambdas are the anonymous `Function`
values defined by RFC 0002. A library may expose `replace_once`, `replace_all`,
or a domain-specific transformation under any name while sharing the same C++
primitive.

## Pattern meaning

The `before` function denotes one rooted expression DAG:

- its arguments are typed holes;
- a repeated argument is an equality constraint, not two holes;
- each call identifies one exact `FunctionDecl` overload;
- Known properties and function references compare by canonical value and
  declaration identity;
- its single returned value is the root;
- blocks, branches, loops, captures, and multiple results are outside the
  first expression matcher.

This interpretation is derived from verified IR. No pattern AST, wildcard
type, pattern opcode, or retained source syntax is introduced.

The `after` function must have the same argument types and one result of the
same type as `before`. Its arguments receive the values bound by the match.
Its expression DAG is cloned into the subject with ordinary `Function::Edit`
operations.

## Match legality

A match is legal only when:

1. every pattern call and Known property matches exactly;
2. the root may have arbitrary external users, all replaced transactionally;
3. a non-root matched result with an external user is preserved rather than
   erased, along with any matched ancestors required by that user;
4. the replacement result has the root's exact type;
5. the matched and replacement expression boundaries carry the same effect
   tokens;
6. every inserted call passes ordinary call-contract verification.

Candidates are visited in deterministic Function order. `replace_all` accepts
a maximal non-overlapping set: once a call belongs to an accepted match it
cannot belong to a later match in the same transaction. The entire operation
commits once. Any diagnostic, failed clone, or failed verification publishes
nothing.

## Effects without annotations

Joggle does not add `pure`, `effect`, trait, or interface annotations to
functions. Effects are explicit SSA values with an ordinary Prelude type:

```joggle
type effect(domain: type);
```

A module defines the domain as a normal type and threads its token through
stateful calls:

```joggle
type memory();
fn store(token: effect<memory>, address: index, value: i8)
    -> effect<memory>;
```

Calls without an effect-typed input or result are pure by the language
contract. External interaction in Residual IR must be represented by a token;
compiler-time host interaction remains behind explicit `@` and is never part
of an expression match.

The verifier enforces affine effect flow: each SSA token has at most one
consuming site. A branch terminator may transfer the same token to mutually
exclusive successors, which receive distinct block arguments; merges likewise
use block arguments. This makes ordering visible in existing SSA/CFG instead
of requiring a parallel effect table.

Source materialization threads every visible effect token through Residual
structured control. This covers direct returns as well as rebinding, and keeps
`if`, `while`, and typed `for` on the same verifier-visible CFG contract.

The first matcher accepts only token-free expression lambdas. The boundary
rule above already defines the later token-aware extension and prevents the
initial implementation from silently rewriting stateful computation.

Template validation walks backward from the returned value. Function
arguments are holes; Known values and function references are leaves; every
call and every hole must be reachable from the root, and each call must have
one result. This rejects dead calls, unused holes, nested inline functions,
effects, CFG structure, and tuple-like calls before matching begins, without
retaining a second pattern representation.

Matching recursively compares the existing typed Values and exact call
declarations. Hole bindings use SSA equality, including repeated-hole
constraints; Known values use their canonical equality; function references
use declaration identity. Pattern-call mapping is injective. Accepted calls
are returned in Function order even when a pure internal result also feeds an
unmatched call.

Replacement first chooses a maximal non-overlapping match set in that order.
It then clones every `after` DAG before changing any root, replaces all roots,
marks matched calls whose non-root results still escape, recursively preserves
their matched producers, erases only the remaining claimed calls in reverse
Function order, and commits once. Repeating the ordinary replacement function
can therefore consume two branches that share an ancestor without duplicating
or prematurely deleting it. Normal `Function::Edit` rollback and module-
closure verification remain authoritative.

## C++ primitive

The low-level operation is intentionally value-oriented:

```cpp
std::optional<std::size_t> replace(
    Function& subject, const Function& before, const Function& after,
    Diagnostics& diagnostics);
```

It returns the number of committed matches. Zero is a successful no-op and
preserves the subject revision. The existing callback-based `rewrite` remains
available for compiler implementation, but users of the source language do
not need it.

Whole-module replacement applies the same primitive to materialized members
on a private `Module` snapshot and publishes only after every member succeeds.

## Rejected designs

- A `rewrite` declaration form duplicates `fn` and staging.
- Pattern opcodes or wildcard values create a second IR.
- Name-based matching ignores overload identity and properties.
- A global purity registry can drift from installable module declarations.
- Implicit effect inference from names such as `store` is not compositional.
- Mutating matches one by one exposes partial results after a later failure.

## Implementation gates

1. [complete] Add `prelude.effect<domain>` and type-system recognition without
   adding a new declaration category.
2. [complete] Enforce affine token use and add branch/merge positive and
   negative tests.
3. [complete] Validate token-free, single-block expression templates.
4. [complete] Implement deterministic typed DAG matching with repeated-hole
   equality and shared-ancestor preservation.
5. [complete] Clone replacement DAGs through one `Function::Edit` and commit
   atomically.
6. [complete] Expose Function and Module C++ overloads and bind them from a
   normal transformation module.
7. [complete] Add fusion, no-match, overlap, wrong-type, shared-DAG, effect
   rejection, rollback, formatting, and source `@replace` end-to-end tests.

No transform module is added before gates 1--5 pass at the C++ level.
