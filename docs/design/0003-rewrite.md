# Design 0003: Expr rewriting

Status: accepted

This record defines structural replacement safety. Design 0007 adds conservative
definitional-equivalence checking before this primitive is used by a normal
compiler-facing mod.

## Purpose

Joggle needs a reusable structural replacement surface without exposing
cursor, anchor, region, pattern, or rewrite-object APIs. The transformation
must reuse the typed `Fn` values completed by Design 0002 and must reject
mutations whose data-flow or effect boundary is not preserved. Semantic uses
such as canonicalization or instruction selection additionally require the
equivalence boundary in Design 0007.

## Source surface

Replacement is an ordinary compiler fn supplied by a mod:

```joggle
fn replace(input: fn, before: fn, after: fn) -> fn;

optimized = @transform.replace(
  input,
  (x: value) => step(step(x)),
  (x: value) => twice(x),
);
```

`replace` is not a keyword. The two lambdas are the anonymous `Fn`
values defined by Design 0002. A library may expose a domain-specific
transformation under any name while sharing the same C++ primitive.

## Pattern meaning

The `before` fn denotes one rooted expression DAG:

- its arguments are typed holes;
- a repeated argument is an equality constraint, not two holes;
- each call identifies one exact `FnDecl` overload;
- Known properties and fn references compare by canonical value and
  declaration identity;
- its single returned value is the root;
- blocks, branches, loops, captures, and multiple results are outside the
  first expression matcher.

This interpretation is derived from verified IR. No pattern AST, wildcard
type, pattern opcode, or retained source syntax is introduced.

The `after` fn must have the same argument types and one result of the
same type as `before`. Its arguments receive the values bound by the match.
Its expression DAG is cloned into the subject with ordinary `Fn::Edit`
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

Candidates are visited in deterministic Fn order. `replace` accepts a
maximal non-overlapping set: once a call belongs to an accepted match it
cannot belong to a later match in the same transaction. The entire operation
commits once. Any diagnostic, failed clone, or failed verification publishes
nothing.

## Effects without annotations

Joggle does not add `pure`, `effect`, trait, or interface annotations to
fns. Effects are explicit SSA values with an ordinary Prelude type:

```joggle
type effect(domain: type);
```

A mod defines the domain as a normal type and threads its token through
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

Template validation walks backward from the returned value. Fn
arguments are holes; Known values and fn references are leaves; every
call and every hole must be reachable from the root, and each call must have
one result. This rejects dead calls, unused holes, nested inline fns,
effects, CFG structure, and tuple-like calls before matching begins, without
retaining a second pattern representation.

Matching recursively compares the existing typed Vals and exact call
declarations. Hole bindings use SSA equality, including repeated-hole
constraints; Known values use their canonical equality; fn references
use declaration identity. Pattern-call mapping is injective. Accepted calls
are returned in Fn order even when a pure internal result also feeds an
unmatched call.

Replacement first chooses a maximal non-overlapping match set in that order.
It then clones every `after` DAG before changing any root, replaces all roots,
marks matched calls whose non-root results still escape, recursively preserves
their matched producers, erases only the remaining claimed calls in reverse
Fn order, and commits once. Repeating the ordinary replacement fn
can therefore consume two branches that share an ancestor without duplicating
or prematurely deleting it. Normal `Fn::Edit` rollback and mod-
closure verification remain authoritative.

## C++ primitive

The low-level operation is intentionally value-oriented:

```cpp
std::optional<std::size_t> replace(
    Fn& subject, const Fn& before, const Fn& after,
    Diag& diagnostics);
```

It returns the number of committed matches. Zero is a successful no-op and
preserves the subject revision. The existing callback-based `rewrite` remains
available for compiler implementation, but users of the source language do
not need it.

Whole-mod replacement applies the same primitive to materialized members
on a private `Mod` snapshot and publishes only after every member succeeds.

## Rejected designs

- A `rewrite` declaration form duplicates `fn` and staging.
- Pattern opcodes or wildcard values create a second IR.
- Name-based matching ignores overload identity and properties.
- A global purity registry can drift from installable mod declarations.
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
5. [complete] Clone replacement DAGs through one `Fn::Edit` and commit
   atomically.
6. [complete] Expose Fn and Mod C++ overloads and bind them from a
   normal transformation mod.
7. [complete] Add positive replacement, no-match, overlap, wrong-type,
   shared-DAG, effect rejection, rollback, formatting, and source
   `@transform.replace` end-to-end tests.

No transform mod is added before gates 1--5 pass at the C++ level.
