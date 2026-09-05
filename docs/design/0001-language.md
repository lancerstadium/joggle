# Design 0001: Language core

Status: accepted

## Purpose

Joggle is a small language and C++ runtime for people who co-design AI
programs and hardware. It must let an extension author describe types,
computation, transformations, importers, analyses, schedules, and exporters
without forcing those activities into unrelated framework classes.

The core is intentionally smaller than a conventional multi-dialect compiler.
It owns reusable language semantics and editable IR. Tensor vocabularies,
hardware formats, scheduling policies, and file formats belong in installable
mods.

## Accepted surface

A source file has four declaration forms:

```text
mod  import  type  fn
```

There is one owner (`Mod`), one executable declaration (`fn`), and one type
declaration mechanism (`type`). There is no separate Program, Graph, Pass,
Rule, Strategy, Schedule, Kernel, Target, Attribute, Interface, or Result
object in the public language model.

Symbolic fns use their symbol as their real name:

```joggle
fn (+)<T>(lhs: T, rhs: T) -> T;
fn postfix (!)<T>(value: T) -> T;
fn ([])(input: view, position: index) -> element;
```

`as +` aliases are forbidden. A symbolic overload and an ordinary overload use
the same lookup, identity, reflection, native-binding, and serialization rules.
Bracket syntax is not a second indexing node in executable IR:
`input[position]` is the ordinary infix fn call `[](input, position)`. This
allows any imported Type to define indexing without a compiler registry.

## Staging

Ordinary calls construct or transform program values. `@call(...)` is the only
source-level stage switch: it requests compile-time execution and must either
produce a value or diagnose why it cannot. Merely knowing every argument does
not silently change an ordinary call into compile-time execution.

Compile-time fns are still `fn`; no `pass`, `analysis`, or macro
declaration is introduced. Their ability to run follows from the call site,
their argument types, and the availability of a source or native body.

## Higher-order transformation

Fns and typed lambdas are values. A transformation receives an ordinary `fn`
or `mod` value and edits the real calls, values, blocks, and nested fn bodies:

```joggle
fn optimize(input: fn) -> fn {
  input = @inline(input);
  return @fuse(input);
}
```

These names are library fns rather than language keywords. Typed lambdas are
ordinary anonymous `Fn` values, so loop and tensor bodies use the same
expression syntax, overload resolution, captures, verification, and editing
as named bodies. Transformations must not leak cursor, anchor, behavior,
pattern, or rewrite-framework objects into the source language.

## Effects and legality

Effects are explicit affine `effect<domain>` values. Calls without an effect
input or result are pure. Reordering, duplication, deletion, and replacement
are legal only when the visible token flow permits them. Effect checking is a
compiler invariant, not a marker interface authors must attach
manually.

## Scheduling boundary

A transformation policy is an ordinary compile-time fn that transforms a mod
or fn. Its component transformations are mod-defined fns; users do not
construct a Schedule class. Correctness must be independent of later candidate
ranking or measurement.

No target or device hierarchy is part of this record. A target-specific mod
may define formats, memory spaces, operations, costs, and exporters using the
same `type` and `fn` mechanisms after the language core is complete.

## Implementation gates

Work proceeds in this order. A later gate may not introduce placeholders for
an earlier gate.

1. One symbol identity: remove operator aliases and canonicalize direct
   symbolic fn declarations.
2. One declaration model: remove `interface`, `attr`, declaration constraints,
   and their C++ reflection/storage paths. **Complete.**
3. Explicit staging: make `@` the only stage switch and test residual calls
   with fully known operands. **Complete.**
4. Higher-order core: add typed fn values and typed lambdas without a
   second expression grammar. **Complete; see Design 0002.**
5. Explicit closures: retain lambda captures as typed dependency edges rather
   than textual substitution. **Complete; see Designs 0002 and 0003.**
6. Direct Fn editing: transform concrete calls and nested bodies without a
   second pattern graph. **Complete for pure, straight-line generic equations,
   including generics inferred below the result root through local definitions
   and multi-result calls; source inference through rebinding and CFG equations
   remain planned. See Design 0003.**
7. Tensor semantics: define logical construction and overloaded access outside
   the core; express traversal and accumulation with ordinary Fn control flow.
   **In progress; static Tensor, indexed `compute`, list/multi-index `[]`, and
   loop-carried accumulation are implemented. See Design 0004.**
8. High-level tensor definitions: express Relu, Conv, MatMul, and similar
   operations in that calculus rather than compiler name cases. **MatMul and
   Relu are the first `nn` definitions; Conv remains planned.**
9. Generic transformation: ordinary two-result equation fns specialize by Type
   and dataflow, including through straight-line local definitions and selected
   multi-result calls. Tensor equations will be rebuilt against the new algebra
   after real NN conversion workloads exist. **In progress.**
10. Frontend validation: preserve unmodified models as source-schema calls,
    convert them to bodyful `nn`, and validate transformed results against a
    trusted runtime. **Import is implemented; conversion remains planned.**

## Deletion policy

Removed syntax and concepts receive no compatibility aliases. Tests and
examples migrate in the same change as the implementation. Documentation may
describe an unfinished gate only when it labels the status explicitly. Old
target, backend, RV, emit, anchor, behavior, and multi-IR designs must not
remain as dormant public files.

## Research claim under evaluation

The prospective systems contribution is not a new pass manager. It is a small,
staged, overloadable language in which AI operations, compiler
transformations, user-controlled schedules, and hardware-specific semantics
compose through one typed fn mechanism. The claim is publishable only if
the implementation later demonstrates lower extension burden, safe
transformation, and competitive compile/run-time behavior on real models.
