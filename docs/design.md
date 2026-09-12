# Design

## Purpose

Joggle is a small compiler workbench for researchers who change neural-network
semantics, loop structure, storage policy, data representation, or target code
generation together. Its job is to make those choices visible and composable
without requiring a new compiler framework for every experiment.

Every stage operates on the same typed function IR. A frontend call, an exposed
tensor body, a loop transform, a storage decision, and target preparation are
different levels of detail in one program, not separate dialects joined by a
mandatory pipeline.

Joggle is pre-1.0 research software. This document defines the stable design;
current coverage and unfinished work are in [roadmap.md](roadmap.md).

## System model

The public model has seven concepts:

```text
Mod  Fn  Blk  Op  Val  Ty  Attr
```

- `Mod` owns a program and its storage.
- `Fn`, `Blk`, `Op`, and `Val` are stable handles into that storage.
- `Ty` is an immutable structural type value.
- `Attr` is immutable compile-time data.

There is no separate graph object. A neural-network graph is the calls and
values in a `Fn`. There is no separate kernel object: exposing a called
function adds ordinary loops, scalar calls, and memory accesses to that same
function body.

The normal path is configurable rather than fixed:

```text
bytes -> codec -> source calls -> semantic functions -> explicit loops
      -> storage/loop transforms -> target preparation -> artifact
```

Every arrow after decoding is an ordinary module function selected by the
user. A program may stop at any useful point, be printed as `.jog`, and resume
later.

## Core IR

Concrete computation is represented as a call. The core distinguishes only the
structural forms needed for evaluation order and ownership:

- call and constant;
- loop and branch;
- return and block yield.

An add, convolution, tensor load, custom number-format operation, device
primitive, or external kernel is a resolved function call. The core contains
neither an operator enumeration nor a class per operation.

A `Fn` owns an entry `Blk` and may own nested blocks through loops and
branches. An `Op` consumes `Val`s and may produce `Val`s. Block arguments
make loop- and branch-carried state explicit. Verification enforces ownership,
dominance, terminators, result arity, and type consistency.

The printer recovers conventional source syntax where possible. Structured IR
remains visible to transforms, but users do not write block arguments or
yields for ordinary `for`, `if`, and `return` source.

## Types and compile-time data

Built-in scalar types and user-defined types use the same structural
representation:

```text
tensor<f32, [1, 3, H, W]>
sat<7>
```

Both are constructor trees. The core validates and matches those trees but
does not assign meaning to every constructor. Modules define constructors and
overloads with ordinary functions:

```jog
fn sat<W: int>() -> Ty;
fn +<W: int>(a: sat<W>, b: sat<W>) -> sat<W>;
```

Generic parameters are compile-time values constrained by ordinary types.
Shapes, widths, layouts, and storage classes can therefore participate in
overload resolution and compile-time computation without a second expression
language.

`Attr` carries booleans, integers, reals, strings, bytes, lists,
dictionaries, types, and handles. Unknown metadata is preserved so an
extension can attach policy without changing the parser or core schema.

## Symbols and resolution

A top-level `fn` is public by default; `local fn` is visible only inside its
declaring module. `use` imports the public surface of another module.

Calls form overload sets. Resolution considers the declaring module and its
transitive dependencies, unifies structural types and generic values, and
selects the most specific unambiguous signature. A call may remain unresolved
while transport semantics are present, but a target boundary must report the
unresolved symbol rather than invent a meaning.

Operators are syntax for functions such as `operator +` and `operator []`.
Adding a number format or tensor access rule extends the visible overload set;
it does not extend the core IR.

## Modules

A module is a directory with `module.jog`, optional sorted `.jog` fragments
under `lib/`, and at most one optional native library under `native/`. The
source is both implementation and public API: public functions are
discoverable, while `local fn` helpers are not exported. There is no generated
header or second manifest to synchronize.

Modules own meaning. The core owns only parsing, IR storage, verification,
symbol resolution, transactional invocation, and deterministic printing.
Bundled modules follow these boundaries:

- codecs decode external bytes into source-level calls;
- bridges refine types and replace transport calls with reusable semantics;
- semantic libraries define tensors and neural-network computation;
- analyses return compile-time values without mutating the program;
- transforms mutate the program transactionally;
- targets state what they accept, prepare unsupported calls, and emit an
  artifact.

These are roles, not hard-coded kinds. A function's signature and behavior
determine how it can be invoked.

## Invocation and transactions

The command-line interface exposes four operations:

- `read` decodes external data into `.jog`;
- `run` invokes one or more mutating functions and prints the module;
- `query` invokes a read-only function and prints its `Attr` result;
- `emit` invokes a function returning text or bytes.

Embedding code uses the same resolution and invocation path. A sequence of
mutations is one transaction: failure restores all IR and metadata changes.
Handles carry liveness and ownership checks, and successful mutations advance
a module revision used by analyses and diagnostics.

This avoids pass subclasses and a global registry. A transform conventionally
has a signature such as:

```jog
fn prepare(m: Mod) -> bool
```

but `dict`, `str`, and `bytes` results are equally ordinary. Parameters are
normal `Attr` values, so policies need no generated option classes.

## Progressive exposure

High-level calls remain useful while a later consumer accepts them. A target
exposes a call only when its capability function rejects it:

```jog
fn accepts(m: Mod, op: Op) -> bool
```

`opt.expose` combines this capability query with available function bodies.
It repeatedly expands unsupported calls until the requested boundary is met or
reports the unsupported frontier. A target therefore needs no central lowering
table, and a new semantic implementation can become usable without editing
target code.

`opt.apply` handles an explicit alternative-function set with the same
overload resolver. A body-bearing function is expanded; a bodyless function
retargets the call and introduces its module dependency atomically. A generic
external declaration remains one `Fn`: artifact modules may derive a concrete
ABI from each resolved call and reject incompatible erasures of the same
symbol. This is the connection between reusable semantic calls and user-owned
low-level computation, not a second kernel or target IR.

Frontend conversion follows the same rule. A codec preserves the source format
faithfully. A separate bridge performs explicit semantic conversion when the
user requests it. ONNX and TFLite do not define canonical neural-network
semantics; they map their schemas to shared functions.

## Transforming computation

Transforms edit `Fn` bodies through the `ir` module. Reusable primitives
cover construction, cloning, movement, replacement, expansion, and fusion. A
policy can be supplied as a normal function handle and invoked through
`ir.invoke`. This separates mechanism from experiment without inventing a
scheduler object or callback ABI.

The bundled `tile` module demonstrates structural loop transformation. Its
operations select explicit loops or producer/consumer pairs and preserve
iteration order, carried state, and use-def consistency. `mem` annotates
reusable tensor storage on the same values. Neither belongs to the core; other
modules may replace either policy.

`opt.specialize` is the smaller mechanism for mixed-stage structure. A module
may mark a loop with an ordinary attribute, then explicitly ask the transform
to expand loops carrying that key and value. Static iteration structure is
removed while dynamic values remain `Val`s; literal-list projection and
constant branch selection then expose direct scalar expressions. This is not
an operator, rank, or target rewrite. The tensor library uses the convention
`[stage: "shape"]` for layout bookkeeping, and the C preparation policy elects
to specialize that convention before emission.

## Targets and artifacts

A target module should make three things inspectable:

1. a capability function describing accepted operations;
2. an explicit preparation function that exposes or rewrites unsupported
   operations;
3. artifact functions returning text or bytes.

The bundled C and deterministic VM modules exercise different artifact paths.
They are modules, not privileged backends. Custom scalar representations can
provide target-specific adapters in separate modules, as the saturating-number
example does, without adding those representations to the core.

Code generation is late and explicit. Storage placement, external payloads,
ABI choices, and target calls remain visible as IR or function arguments before
emission. SystemVerilog, custom RISC-V code, and cycle models are possible
module outputs, not core concepts.

## Invariants

1. There is one public function IR and one textual representation.
2. Concrete computation is a call; the core has no operator catalogue.
3. Types and attributes are structural values, not target-specific classes.
4. Public extension points are functions resolved through module imports.
5. Frontend conversion and target preparation are explicit user-selected
   calls.
6. A failed invocation leaves the module unchanged.
7. Unknown metadata survives parsing, printing, and unrelated transforms.
8. Printed output and module discovery are deterministic.
9. Target vocabulary does not leak into the core API.
10. A target diagnoses its unsupported frontier before emission.

## Non-goals

Joggle is not currently:

- a production inference runtime;
- an automatic schedule search system;
- a replacement for mature target toolchains;
- a universal hardware description or cycle simulator;
- a promise that one schedule performs well on every device.

Those facilities may be integrated as modules or external tools. Keeping them
outside the core lets the workbench remain small enough for a researcher to
understand and modify.

## Evidence standard

Project and paper claims use distinct levels of evidence:

1. **Parse:** source or external data can be decoded.
2. **Resolve:** calls and types close under the selected module set.
3. **Convert:** source-format calls map to shared semantics.
4. **Expose:** required bodies become explicit loops and scalar calls.
5. **Execute:** an emitted artifact matches a reference within a stated error.
6. **Improve:** a controlled comparison shows a resource or performance gain.

Passing one level does not imply the next. Negative measurements are retained
as design evidence, not rewritten as successful optimization claims. The
evaluation plan and current evidence ledger live in
[paper/README.md](../paper/README.md).
