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
modules.

## Accepted surface

A source file has four declaration forms:

```text
module  import  type  fn
```

There is one owner (`Module`), one executable declaration (`fn`), and one type
declaration mechanism (`type`). There is no separate Program, Graph, Pass,
Rule, Strategy, Schedule, Kernel, Target, Attribute, Interface, or Result
object in the public language model.

Symbolic functions use their symbol as their real name:

```joggle
fn (+)<T>(lhs: T, rhs: T) -> T;
fn postfix (!)<T>(value: T) -> T;
```

`as +` aliases are forbidden. A symbolic overload and an ordinary overload use
the same lookup, identity, reflection, native-binding, and serialization rules.

## Staging

Ordinary calls construct or transform program values. `@call(...)` is the only
source-level stage switch: it requests compile-time execution and must either
produce a value or diagnose why it cannot. Merely knowing every argument does
not silently change an ordinary call into compile-time execution.

Compile-time functions are still `fn`; no `pass`, `analysis`, or macro
declaration is introduced. Their ability to run follows from the call site,
their argument types, and the availability of a source or native body.

## Higher-order transformation

Functions and typed lambdas are values. The first transformation primitive is
expression replacement:

```joggle
@transform.replace(
  input,
  (x: value) => step(step(x)),
  (x: value) => twice(x),
);
```

The two lambdas use normal expression syntax and normal overload resolution.
The compiler derives the match graph from the first lambda and checks both
sides before mutation. Structural CFG editing is a later, typed quotation
facility; it must not leak cursor, anchor, behavior, or rewrite-framework
objects into the basic API.

## Effects and legality

Pure tensor values and mutable views are distinct types. Effects are inferred
and checked from called functions. Reordering, duplication, deletion, and
replacement are legal only when the involved effects permit them. Effect
checking is a compiler invariant, not a marker interface authors must attach
manually.

## Scheduling boundary

A transformation policy is an ordinary compile-time function that transforms
a module or a function. Its component transformations are module-defined
functions; users do not construct a Schedule class. Correctness must be
independent of later candidate ranking or measurement. Design 0007 defines the
first semantic-equivalence boundary for user-defined optimized functions.

No target or device hierarchy is part of this record. A target-specific module
may define formats, memory spaces, operations, costs, and exporters using the
same `type` and `fn` mechanisms after the language core is complete.

## Implementation gates

Work proceeds in this order. A later gate may not introduce placeholders for
an earlier gate.

1. One symbol identity: remove operator aliases and canonicalize direct
   symbolic function declarations.
2. One declaration model: remove `interface`, `attr`, declaration constraints,
   and their C++ reflection/storage paths. **Complete.**
3. Explicit staging: make `@` the only stage switch and test residual calls
   with fully known operands. **Complete.**
4. Higher-order core: add typed function values and typed lambdas without a
   second expression grammar. **Complete; see Design 0002.**
5. Effect-safe replacement: implement checked expression matching and atomic
   replacement. **Complete; see Design 0003.**
6. Tensor module: define real tensor types and operations outside the core.
   **Complete; see Design 0004.**
7. ONNX module: load an unmodified ONNX model-zoo artifact through an ordinary
   compile-time function. **Complete; see Designs 0005 and 0006.**
8. Open transformation research: relate executable user kernels to portable
   semantics before adding transformation control, formats, or measured
   selection.
   **In progress; see Design 0007.**

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
compose through one typed function mechanism. The claim is publishable only if
the implementation later demonstrates lower extension burden, safe
transformation, and competitive compile/run-time behavior on real models.
