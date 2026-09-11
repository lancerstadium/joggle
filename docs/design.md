# Design

## Purpose

Joggle is an embeddable compiler workbench for neural-network and AI hardware/
software co-design experiments. Its core is infrastructure, not a fixed
optimization method or deployment stack.

The engineering hypothesis is falsifiable: one structured function IR and one
typed native-function extension boundary should support model import, graph and
loop transforms, custom data formats, and target experiments without changing
the core, adding per-operator classes, or maintaining a second public IR.

## Concept model

The complete public IR vocabulary is:

```text
Mod  Fn  Blk  Op  Val  Ty  Attr
```

`Mod` owns storage. `Fn`, `Blk`, `Op`, and `Val` are stable handles. `Ty` and
`Attr` are immutable structural values. Concrete computation is always a call
to a `Fn`; only calls, constants, loops, conditions, returns, and `Blk` yields
are structural operations.

A model graph is a view of calls and values in a function body. A lowered
kernel is the same kind of function with explicit loops, indexing, storage, or
target calls. Neither requires another durable IR.

`Env` is not part of the IR. It owns module search paths, loaded declarations,
native function bindings, and environment diagnostics. Environments are local
objects, never global registries.

Loading one requested module is an environment transaction over its full
dependency closure. If a dependency, parent verifier, native library, or native
initializer fails, every module, binding, library handle, loading marker, and
epoch change introduced by that request is restored. Previously loaded state
remains intact and diagnostics remain available.

## Extension boundary

Modules declare types and functions in `.jog`. An optional native library may
attach an implementation to any external function declaration through one
size-checked C ABI. The declaration remains the single source of its signature.
The exported entry is always `joggle_module`; ABI evolution is represented in
the API record instead of encoded in public symbol and type names.

Functions, operations, and values use the same open `Attr` dictionary and
square-bracket syntax. Function attributes describe declarations and entry
policy; operation attributes describe computation such as placement or
scheduling; value attributes describe data such as quantization, layout,
sparsity, range, and source identity. The parser reserves no attribute name.
Attributes are inert until an explicitly selected module function reads them;
even names such as `host` have no privileged behavior. Native binding depends
only on an external declaration and a matching native symbol; it needs no
marker.

An importer, transform, analysis, simulator, or emitter is therefore an
ordinary compile-time function. `run` interprets the same structured function
body over compile-time scalars, lists, and IR handles. The `ir` module exposes a
small, representation-complete reflection vocabulary; it is not another IR or
an operator catalog. Core has no codec, pass, target, device, analysis, emitter,
or artifact class family.

Compile-time execution is explicit, deterministic, and in-place on success.
It has no ambient file, network, clock, or process access. A failed function or
invalid result restores the input `Mod`, so experiments do not leave partially
rewritten IR behind.
The embedding run boundary may supply additional structural `Attr` arguments
after `Mod`. They participate in normal overload resolution and appear in the
deterministic step report. This keeps parameterized selection and scheduling
policy in ordinary functions rather than generated pass-option classes.

## Invariants

- Core depends only on the C++20 standard library.
- Adding computation never adds an `Op` subclass or parser case.
- Adding a module never generates or recompiles a core header.
- Mutations go through `Mod` and preserve handle/use-def integrity.
- A successful compound edit advances the module revision exactly once,
  including an environment-aware expansion that also adds module visibility.
- Verification commits inferred types only when every module invariant holds;
  a changed commit advances the module revision exactly once, while failure
  restores the prior types and retains the diagnostics.
- Call insertion names its existing insertion point and rejects operands that
  do not dominate it; no mutable global builder state is required.
- Unknown external symbols remain printable; operations that require their
  semantics diagnose the missing dependency.
- User-visible text never exposes generated SSA names.
- Canonical printing is deterministic and round-trips structurally.

## Non-goals

Core does not contain ONNX, tensors, NN operators, devices, memory hierarchies,
instruction sets, code generators, runtimes, JITs, e-graphs, solvers, schedules,
or cloud/edge policy. It is not a general-purpose language or package manager.

Those capabilities may be removable modules after the structural core proves
it can host them. A research mechanism becomes a paper contribution only after
its own method and experiments justify that claim.

## Gates

1. A generic, nested-loop matrix multiplication must parse, print, reparse, and
   verify without another IR.
2. A C++ native function and a `.jog` compile-time function must both rewrite the
   same function representation.
3. An ONNX module must import an official model without a core operator switch.
4. A target module must add a number type, primitive, selection transform,
   simulator, and emitter with zero core changes.

Any failed gate reopens the relevant boundary; it does not justify silently
adding another framework layer.

## M1 baseline

The first working M1 slice was measured on macOS with AppleClang 17 in Release
mode on 2026-09-09. A clean configure took 0.25 s and a parallel build took
2.10 s. Peak build resident memory was approximately 230 MB. The resulting
dynamically linked CLI was 292,456 bytes and the static library was 396,728
bytes. The installed header plus production source and CLI contained 2,466
lines.

These numbers are a local regression baseline, not cross-machine performance
claims. Tests and the sample native module are excluded from the source-line
count. The build made no network requests and used no third-party library.

## M2 slice

M2 adds collection iteration and transactional compile-time function execution.
`modules/opt/module.jog` implements add-zero folding using only normal language
forms and `ir` reflection calls. The workflow test applies the same rewrite once
from direct C++ traversal and once from the textual function, then checks a
forced post-edit failure rolls back byte-for-byte to the canonical input.

## M3 contract

The external frontend gate uses the official ONNX Model Zoo catalog.
`test/zoo.cmake` downloads immutable artifacts directly from one pinned commit
in the official ONNX Model Zoo GitHub media store, then validates the SHA-256
values published by its Git LFS manifests. Configure and normal builds remain
offline.

The normal matrix contains `mobilenetv2-7`, `squeezenet1.1-7`,
`squeezenet1.0-13-qdq`, `resnet18-v1-7`, `tinyyolov2-8`,
`tiny-yolov3-11`, `ultraface-rfb-320`, `ssd-mobilenetv1-12`,
`shufflenet-v2-12`, `densenet-12`, `googlenet-12`, and both the QDQ and
operator-oriented INT8 encodings of `efficientnet-lite4-11`. These are
deliberately different topology classes: separable convolution with residual
paths, Fire `Blk`s with concatenation, a full QDQ network, a residual
classification backbone, a compact detector using max pooling and leaky
activation, a detector post-processing graph with four `Loop` bodies, and a
small face detector with a large dynamic shape program. SSD-MobileNetV1 adds a
full detection pipeline with 1,567 constants, 5,985 nodes, eight nested graphs,
Resize, and NonMaxSuppression. ShuffleNet V2 adds channel split/shuffle
structure, DenseNet-121 adds a long concatenative dependency graph, GoogLeNet
adds LRN and a two-result Dropout, and the two EfficientNet encodings expose
the difference between QDQ and QLinear normalization boundaries.
MobileNetV2 remains the deep semantic gate; the next four and UltraFace all
pass binary import, canonical round trip, fixed-point type inference,
relationship conversion, idempotence, verification, and converted round trip.
Its 155 semantic calls are also expanded as one atomic batch and the resulting
loop-level module must verify and round-trip. This keeps the scale gate on an
official application model rather than a synthetic graph.
Tiny-YOLOv3 remains a pinned partial-frontier gate. It imports
269 tensors, 291 calls, and four nested functions, then reduces 280 open results
to 219. UltraFace imports 244 tensors and 242 calls and closes all 240 initially
open results. Its full conversion gate also exercises tensor-valued Constant,
legacy attribute-form Slice, and Softmax.
SSD-MobileNetV1 is a pinned type-closure gate. Generic capture and loop-carried
parameter propagation plus conservative partial shape relations close all
6,790 initially open results at a checked fixed point; this does not claim that
every source operation has shared executable semantics. ShuffleNet V2 closes
273 open results and completes conversion and round trip. DenseNet-121 is
tested after all 910 intermediate result annotations are erased: its signature,
constants, and schema relations reconstruct every result type. The same erased-
signature gate closes all 144 GoogLeNet results and all 539 EfficientNet-QDQ
results. The operator-oriented EfficientNet INT8 model deliberately pins a
95-result frontier headed by `QLinearConv`; it is a normalization requirement,
not mislabeled support. BiDAF is a separately enabled heavy codec gate because
its large vocabulary attribute tests parser and canonical-text scalability
rather than ordinary semantic coverage.

Inspection with the official ONNX 1.19 schema reports IR version 3, opset 7,
155 nodes, 267 initializers, 268 declared inputs, and one graph output. All 155
nodes have one output. The graph contains Conv, BatchNormalization, Relu, Add,
GlobalAveragePool, and Reshape; all initializers use typed fields rather than
`raw_data`. The importer must therefore exclude initializer-backed legacy graph
inputs and normalize typed tensor payloads without losing their bits.

The ONNX module uses Protobuf as an optional module dependency, not a core
dependency. It maps graph inputs to function parameters, initializers to
typed tensor constants with preserved bytes, nodes to calls named by ONNX
domain and operator, node attributes to structural `Attr` dictionaries, and
graph outputs to returns. Operator meaning is not decoded by a core switch.
Nested graphs follow the same mapping recursively. Their lexical captures are
made explicit as trailing function parameters and appended call operands; the
graph attribute records only the referenced function, its formal input count,
and capture operand positions. No ONNX-only region or control-flow container is
added to core.
The same codec boundary is now independently exercised by the TFLite module.
Semantic conversion then removes the codec-specific constant operations:
ONNX initializers, ONNX Constant payloads, and TFLite buffers become the same
result-typed `tensor.literal(bytes)` call. The tensor module owns that data
primitive, while C and VM interpret it independently. This prevents target
modules from accumulating frontend cases and preserves raw source bits until a
representation-aware consumer is selected.

The pinned MobileNetV2 model passes the complete codec gate: binary decode,
generation of 267 tensor constants and 155 calls, parse, verify, canonical
print, reparse, and structural equality. Its 14,156,560 bytes of initializer
payload are preserved in typed tensor constants. A Release run on the M1 reference machine
imports and prints the self-contained 28.4 MB module in approximately 0.49 s;
the second parse-print takes approximately 0.31 s. These are local regression
measurements, not general performance claims.

## M4 slice

The optional `sat` module is the extension-boundary gate. Its `sat<W>` type and
`sat.add` primitive are ordinary signatures. Its textual `sat.supports` and
`sat.select` functions inspect the structural type directly; integer additions
remain untouched. Two native scalar functions execute the saturating reference
semantics and emit a concrete SystemVerilog adder.

SystemVerilog is an output of that removable module, not a core backend or IR.
After structural type reflection completed the boundary, the whole format,
policy, simulator, and emitter were added without changing the core
library, parser, evaluator, public header, or operation vocabulary.

The separate `sat.c` composition module exercises the inverse direction: it
recursively maps concrete format types to ordinary C storage types, specializes
one generic saturating body per width, and retargets only resolved format calls.
The C module gained general support for a local single-result call used directly
as an expression, but no `sat` name, width, or format policy. Compiled boundary
tests at 5, 8, and 12 bits distinguish semantic lowering from a type-only demo.

## M5 slice

M5 turns open metadata and reflection into construction rather than mere
inspection. A module can use arbitrary function metadata to select work, query
users, insert any call, replace uses, and erase the old operation. Dynamic list
literals allow the same source language to collect IR handles.

The same boundary now supports module-owned semantic relations. `ir.where`
filters ordinary `Fn` values using open metadata, and `ir.invoke<R>` executes
the uniform `fn(Mod, Op) -> R` shape inside the current transaction. Relation
modules choose `R = bool`; analysis modules may select another ordinary result
type. The evaluator knows neither the attribute key, operator vocabulary, nor
the meaning of the returned value. The
`onnx.nn` module uses `[on: ...]` for both inference and conversion, and a
module-owned `phase` value preserves its two conversion sweeps. Its
two-argument `convert` overload accepts an explicit `Fn` list, so a separate
module can contribute a new conversion relation without editing the built-in
bridge. It filters inference, compute conversion, and shape conversion phases
itself; callers compose function sets rather than copying the driver. Another
module may choose different metadata and policy without a registry, callback
class, or parser extension.

The independently authored `tflite.nn` bridge uses the same two primitives for
semantic conversion, despite different source metadata and layout rules. Its
quantization guard remains module policy around relation selection. This is the
cross-frontend gate for the abstraction: neither frontend contributes a name,
enum, or dispatch case to the evaluator.

The operator-neutral region primitive computes live-ins and a single live-out
for an ordered call region, enforces dominance and motion safety, preserves the
visible result name, and commits the fusion atomically. A visible replacement
callee must resolve through the same generic argument and result typing as an
ordinary call; an unresolved callee stays open for experimental modules. The
generic textual `opt.fuse` helper follows a user-supplied callee sequence; it
contains no ONNX or NN operator names. A test-only bridge applies the helper to
the pinned MobileNetV2 import and replaces 36
Conv-BatchNormalization-ReLU chains with 36 user-named calls, reducing those
108 calls to 36. The optimized 28.4 MB module then verifies, prints, reparses,
and remains structurally equal.

## M6 first slice

`Ty` now preserves canonical text while exposing a recursive constructor tree.
Both public constructors are structural boundaries: `Ty(text)` parses external
text, while `Ty(name, args)` builds a checked tree directly. Generic
substitution and inferred list types use the latter internally, so normal type
rewrites do not serialize and reparse their children.
Known local and qualified calls are resolved against their module declaration;
generic bindings are inferred structurally and substituted into result types.
The verifier also propagates list element and region argument types to a fixed
point, so compile-time traversal code is checked with the same mechanism as
model code. Qualified resolution follows only explicit and transitive `use`
edges, preventing unrelated modules already present in an `Env` from changing
the meaning of a source file.

Parametric type constructors also remain functions:
`fn tensor<E: Ty, S: list<int>>() -> Ty` defines the arity, argument
constraints, and ownership of `tensor<E, S>`. Generic parameters are the same
typed `Val`s used elsewhere, so no parallel kind or trait objects are needed.
The verifier recognizes the `Ty` result instead of a special declaration kind
or registration hook. This is the first self-hosting step toward using ordinary
compile-time values to define and validate richer data formats. Explicit and
inferred generic bindings are also materialized in textual compile-time
function frames, covering integers, type values, and recursively typed lists.

Function names now map to ordered overload sets rather than one declaration.
The same structural matcher handles ordinary calls, symbolic functions such as
`fn +`, textual compile-time execution, public `Env::resolve`, and native
function families. Selection is independent of declaration order: incompatible
signatures are removed, structurally specific signatures outrank generic ones,
and ties are rejected. The base operator declarations provide general algebra;
the `sat` module adds a more specific overload without modifying the parser,
verifier, evaluator, or operator representation.

A call inside a generic body may depend on that body's compile-time parameters.
The verifier retains such a call as unresolved only when a visible overload is
structurally possible after masking those dependent terms. It does not select a
concrete overload early. Specialization substitutes the terms and returns to
ordinary overload resolution; concrete mismatches remain errors. The `math`
module consequently declares exact `f32` and `f64` primitives rather than
pretending every `Ty` supports transcendental functions, while generic `nn`
bodies remain reusable by compatible user-defined scalar overloads.
Function cloning and body expansion immediately revisit copied calls in
definition order. Any result made resolvable by generic substitution is typed
inside the same IR edit; printing and reparsing are not required to close the
new body. C and VM recognize portable primitives through the resolved function
symbol, so qualified and overload-extensible source spellings have identical
target meaning.

The generic `sat.add<W: int>` declaration is the first module-defined
parametric gate: inferred and explicit widths succeed, conflicting widths,
wrong parameter types, and wrong arity fail with located diagnostics, and no
saturating-arithmetic case exists in the core. Language normalizations
(`base.copy`, list construction, and indexing) retain only the minimal
intrinsic rules needed to recover ordinary source bindings and iteration.

## M7 first slice

A call now has zero or more ordered results in both the public C++ editor and
textual reflection boundary. The five-argument `ir.call` name is overloaded by
its last parameter: `Ty` preserves the concise one-result form, while
`list<Ty>` returns the created `Op`. Results remain ordinary `Val`s and are
named with the overloaded `ir.rename`; no tuple operation or result wrapper was
added. Construction transports structural types directly instead of printing
and reparsing them.

Source destructuring uses `let a, b = f()` and works in model code and
compile-time execution. Optional result annotations such as
`let a: i32, b: bool = source()` preserve types across canonical text for open
calls and are checked against known declarations. Thus binary frontends and
later semantic modules can transport multi-result operations without a
frontend-specific core case.

## M7 second slice

Open attributes now apply uniformly to function declarations and visible
operation statements. Calls, loops, conditions, and returns preserve their
attribute dictionaries through canonical printing, and both C++ and `.jog`
transforms can query or edit function and operation attributes. This
generalizes the useful `[name: value]` notation without reserving `host`,
target, scheduling, or layout concepts in the core.

## M7 third slice

`Fn::ops`, `Mod::ops`, and the overloaded `ir.ops` provide one deterministic
structural-preorder walk without forcing every transform to spell three nested
ownership loops. `Blk`-local traversal remains available when locality matters.
Use replacement now has an optional user operation, so a transform can redirect
one edge without rewriting every consumer; both forms reject type or dominance
violations before mutation. `Mod::revision` advances after successful edits and
is restored by failed compile-time runs, providing the invalidation key needed
by later cached analyses. Zero-result calls are visible statements rather than
unprintable hidden operations.

## M7 fourth slice

The editor now constructs typed constants, deep-clones operation subtrees, and
moves operations within a `Blk` through explicit `Op` positions. Loop and
branch construction validates every carried value before changing any source
binding from `let` to `var`, so a rejected structure is byte- and
revision-stable. Deep cloning
creates fresh results, `Blk`s, and `Blk` arguments, remaps internal dataflow,
freshens a copied declaration at the insertion scope, and works for nested
loops and conditions. Structured carried names remain attached to their
existing mutable binding. A clone is therefore printable and valid before its
source is replaced or erased; erasing the replaced source
recursively invalidates its complete subtree. Motion is atomic and checks the
whole module's dominance before commit. The identical operations are available
through `ir.constant`, `ir.clone`, and `ir.move`, with `ir.kind` and
`ir.blks(op)` completing structural discovery. Named constants now remain
named when printed instead of being silently duplicated as inline literals.

Function cloning uses the same `ir.clone` name rather than adding a builder or
template object. It copies one complete generic `Fn`, including nested
control flow and open metadata. Cross-module copies make the source dependency
visible atomically, preserve unambiguous call spelling, qualify only collisions,
and redirect self-recursion to the copied function. Alpha-equivalent overload
signatures are rejected before mutation. C++ and `.jog` exercise the same
primitive, including failure stability and canonical round-trip.
The optional concrete-generic overload performs monomorphic materialization
without adding a specialization object. It validates bindings through the same
overload resolver as an ordinary call, substitutes the signature and complete
nested body, removes the generic parameters, and materializes integer, Boolean,
and recursively typed list operands in the new entry `Blk`. Unsupported
first-class compile-time objects, arity mismatches, open terms, and collisions
with an existing concrete overload roll back both dependency and IR changes.
Function rename updates only calls that resolve to that exact overload, keeps a
short call when it remains unambiguous, and otherwise qualifies it. Function
erase rejects live callers, then invalidates the complete owned
`Fn`/`Blk`/`Op`/`Val` tree in one revision.
Function result contracts are editable through the overloaded `ir.returns`;
parameter and body types continue to use `ir.type`. Verification checks every
return owned by the function rather than only its final entry-block return, so
an early nested return cannot bypass a format-lowering contract.

## M7 fifth slice

Direct construction now covers loops and two-way conditions as well as calls
and constants. A loop constructor creates iterator and carried `Blk` arguments
plus a valid forwarding `yield`; a branch constructor creates two such arms.
The generic argument mutator reconnects calls, loops, branches, returns, and
yields with arity, type, and dominance checks. Consequently a textual module
and embedding code can each build the same nested-loop-plus-condition function
from a one-return seed, populate its bodies through their terminator insertion
points, print ordinary source, and round-trip it. The textual path also proves
that a failure after editing restores the complete nested structure and its
revision. No public `Blk` builder, region descriptor, or source-form enum was
introduced.

Source construction canonicalizes carried state before exposing the module.
Only a mutable binding whose yielded value differs from its corresponding
block argument remains a loop or branch result. Read-only uses are reconnected
to the dominating outer value, unused provisional values are reclaimed during
parsing, and the final value store is compacted once. This is a property of the
single structured IR, not an effect annotation, optimization pass, or second
control-flow representation.

Boolean `&&` and `||` use the same branch representation rather than eager
operator calls. Only the selected arm evaluates its right operand; canonical
printing reconstructs the compact expression, and structural traversal still
observes its nested `Blk`s. Yielded values are verified against their carried
result types for both parsed and programmatically constructed control flow.

## M7 sixth slice

Value renaming is now closed over structured control flow. Renaming any member
of a loop- or branch-carried value chain updates its entry value, `Blk`
arguments, yielded versions, and operation results together; renaming a loop
iterator also updates the loop header. This keeps the single `Val` API honest:
there is no separate `Blk`-argument naming hook, and every successful edit
still prints as valid ordinary source. Binding names accepted by the editor are
restricted to source-safe, non-keyword identifiers.

## M7 exit gate

The workflow test now has C++ and `.jog` implementations construct byte-for-byte
identical nested-loop-plus-condition source from the same one-return seed. It
also exercises multi-result creation, deep structured cloning, `Blk`-local
motion, selective replacement, carried `Blk`-argument renaming, and rollback
after a nested-IR edit. The strict Release build, address sanitizer build,
installed-header consumer, and pinned official ONNX model all pass. M7 is
therefore closed; later additions to editing must be justified by a concrete
module rather than by expanding a generic builder surface.

## M8 first slice

The first composition slice stays entirely in ordinary module functions.
`opt.fold_identity`, `opt.cse`, and `opt.dce` are directly callable transforms;
`opt.fix` composes them into a caller-bounded fixed point, and `opt.basic` is a
zero-policy entry point. CSE and dead-call elimination require an explicit list
of pure callees. This makes effect assumptions visible at the call site and
keeps unknown frontend or target calls conservative without adding an effect
class, trait, or privileged attribute to the core.

The bound is failure policy rather than permission to return an incomplete
result. Each round compares the module revision; if the final allowed round
still edits IR, `opt.fix` diagnoses non-convergence and the ordinary outer
transaction restores the exact input. `opt.basic` uses the operation count plus
one as a graph-derived bound. A reverse dead-use chain exercises both rollback
under a deliberately tight bound and complete cleanup under a sufficient one.

Deletion-aware transforms use `ir.live` when iterating an earlier operation
snapshot. `ir.blk` and whole-dictionary `ir.meta` provide the remaining
structural equality inputs: CSE only merges calls in one `Blk` with identical
callee, operands, result types, and metadata. The test pipeline merges repeated
open calls, removes newly dead calls to a fixed point, preserves calls whose
metadata differs, and is revision-idempotent on a second run.

While exercising composed conditions, the canonical printer was also made
precedence-aware. It now restores the minimal parentheses needed to preserve
operator trees, including right-nested operators of equal precedence.

## M8 second slice

Embedding code may request a deterministic execution report through the
four-argument `run(env, function, mod, report)` overload. The report is an
ordinary structural `Attr` dictionary rather than a pipeline, result, or event
class. It distinguishes the function's returned `reported` flag from actual
IR mutation (`changed` and `edits`, derived from revisions) and lists every
nested function that accepted the same `Mod` in completion order. Each entry
has an additive `kind` field. Generic body expansion adds an `expand` entry
with the resolved source symbol, concrete implementation symbol, full overload
type patterns, and revision delta, so selection evidence comes from the actual
edit rather than a parallel planning engine. The original three-argument call
remains the terse success/failure API.
Function-level `ir.clone` contributes the analogous `clone` entry with source,
copy, parameter and return patterns, and the exact revision interval. Thus a
template-materialization experiment observes the committed edit rather than a
separate proposed plan.
The command line exposes this exact report with `--report <file>`. It keeps
canonical `Mod` output on stdout and uses the same public `print(Attr)` overload
as embedding code, preserving one reporting representation.

Compile-time entry points are now checked against the promised
`fn(Mod) -> bool` contract before execution. A false return still means “ran
successfully but reported no change”; malformed entry signatures and runtime
failures remain failures and roll the module back.
Entry selection now resolves the supplied name against the actual `Mod`
argument, just like ordinary source calls and read-only queries. Additional
overloads no longer hide a valid host entry merely because the symbol is not
globally unique.

## M8 third slice

Analyses are ordinary source functions too. The embedding call
`query(env, function, mod, result, args, cached)` resolves a textual function
whose first parameter is `Mod`, passes any remaining inputs as structural
`Attr` values, and requires exactly one `Attr`-representable result. Calling
through `query` is the explicit read-only promise; no `[pure]` tag, analysis
base class, or second declaration form is involved.

The evaluator receives a private verified snapshot. A query that edits that
snapshot is rejected, while the caller's module remains untouched. Successful
results are cached against the environment identity and load epoch, module
revision, applied function spelling, and explicit inputs. Every IR mutation
clears the module-local cache, and loading another source/native module advances
the environment epoch, so cached overload resolution cannot survive a changed
function environment. `opt.count` is a small reusable example rather than a
privileged analysis primitive.
The command line exposes the no-extra-argument subset through
`joggle query module.fn model.jog`. It prints the same canonical `Attr` as the
embedding overload. `opt.untyped` uses this path to report distinct calls with
open result types, complementing the symbol-oriented `opt.unresolved` query.

`joggle emit module.fn model.jog` deliberately reuses this query boundary. It
accepts only `str` or `bytes` and writes the payload verbatim, so text and
binary generators remain read-only module functions. Core does not learn an
artifact hierarchy, target kind, filename convention, or emitter lifecycle;
shell redirection and embedding code decide where the returned bytes go.

## M8 fourth slice

Embedding code can assemble an ordered transform sequence by passing a
`span<string_view>` to `run`. This is an overload, not a pipeline object: each
name still selects an ordinary `fn(Mod) -> bool`, and source code composes the
same functions by calling them normally. The sequence reports its constituent
function reports as structural `Attr` values and derives aggregate change/edit
information from the module revision.

The outer execution is transactional. If a later function is missing, has the
wrong signature, fails, or leaves invalid IR, all earlier edits in that
sequence are rolled back together. The implementation takes one rollback
snapshot for the complete sequence; it does not copy the module again at every
step. Each step is still verified and receives the same report it would have
received through the single-function overload. The workflow test runs the same
two functions once through the C++ sequence overload and once through a
textual wrapper, then requires byte-identical canonical IR; a deliberately bad
second step exercises whole-sequence rollback.

Nested execution traces retain functions that actually advance the module
revision plus explicit expansion and cloning events. Read-only predicates and
unchanged helpers are omitted, so a structural capability check over every
operation does not turn a preparation report into an operation-sized call log.
The top-level step still reports its returned Boolean and before/after revision.
The embedding and CLI paths do not allocate a trace at all when no report was
requested; report collection is observational rather than a mandatory cost of
running a transform.

## M8 fifth slice

Capability-driven exposure remains a library mechanism. `opt.legalize` accepts
a list of ordinary `Fn` declarations and a round bound, keeps calls whose
resolved symbol and signature the consumer accepts, and expands every other
metadata-free call whose resolved `Fn` has a body. `ir.name(Fn)` supplies the
declared semantic symbol, while `ir.accepts` reuses the same recursive generic
unifier as normal call resolution. A capability can therefore constrain
element type, rank, dimensions, and a user-defined structural type without a
parallel legality language. `opt.frontier` reports the deterministic, distinct
remainder through the existing read-only query path.

The mechanism adds no target, kernel, legality, or pattern object to core. A
consumer module writes bodyless functions whose local names mirror canonical
source symbols, obtains them with `ir.fns(module)`, and wraps `opt.legalize` in
a normal `fn(Mod) -> bool`. The capability module is inspected but is not added
to the model's dependency closure. Calls without definitions and calls whose
operation metadata needs an explicit policy remain visible. A network test
retains `nn.relu` for `tensor<i8, [4]>` while expanding the same symbol for
`tensor<f32, [4]>`, then checks canonical round-trip stability.

A complementary overload accepts one ordinary `fn(Mod, Op) -> bool` predicate.
`opt.expose` composes static folding, copy removal, and one layer of exposure at
a bounded fixed point; `opt.legalize` offers exposure alone and `opt.frontier`
reports the rejected calls. The predicate may inspect any reflected structure,
but it must not mutate the module. Revision checking enforces that rule and the
normal outer transaction restores the complete input on violation. C and VM
use this same mechanism with different predicates, so accepted computation is
target-owned policy rather than a core target interface or duplicated operator
declarations.

The same explicit `Fn` set can supply implementations. `ir.match` performs the
ordinary overload ranking inside that set, honors generic arguments written on
the source call, and rejects a selected function when its substituted results
conflict with known call results. Its single-function overload returns the
ordered inferred generic terms, so an extension can materialize that concrete
function through `ir.clone` without a second inference path. The
environment-aware `ir.expand` permits a function whose local name equals the
source call's resolved symbol. It adds the implementation module only when not
already visible, then specializes and copies the normal function body.
Dependency and body edits roll back together. `opt.apply` is merely the bounded
fixed-point policy over these primitives: it skips bodyless declarations and
metadata-bearing calls, rejects ambiguous or non-converging implementation
sets, and contains no target or NN names. The general `base.assert` primitive
turns bound exhaustion into a located transactional failure. A network
regression chooses a shape-specialized i8 ReLU implementation over a generic
i8 overload, follows a second implementation layer at another shape, and
proves a recursive implementation restores the exact input and revision. Its
ordinary execution report also proves all three selected expansions, including
both overload signatures and the intermediate implementation layer. A separate
call-driven materialization gate resolves a generic ReLU, reflects its concrete
element and shape terms, retains a monomorphic local function, and retargets
the call; a forced failure after cloning leaves no orphan function.

The executable `examples/ikj` package applies the same mechanism to a familiar
kernel decision. Its single generic alternative changes a rank-two matrix
multiplication from `i-j-k` to `i-k-j`, after which the unchanged C preparation
and emitter compile and numerically execute it. The package is intentionally
outside `modules`: it is a user extension and no project build is needed to
discover it with `-M examples`. This case establishes inspectable kernel-body
control, not a performance advantage for that loop order.

The independent `examples/edge` package exercises the non-expanded boundary.
A bodyless monomorphic `edge.matmul` declaration with fixed tensor types is the
entire external ABI contract. The C module derives a qualified dependency
prototype, emits the model wrapper, and links a separately compiled kernel.
Anonymous tensor results receive ephemeral names from their structural value
keys, so a direct `return edge.matmul(a, b)` needs no cosmetic binding. Generic,
dynamic-shape, multi-result, or otherwise unrepresentable declarations remain
outside this ABI instead of being guessed. Core contains neither the `edge`
symbol nor an external-kernel registry.

## M8 sixth slice

Optional embedding overloads measure each explicitly selected function with a
steady clock. A single-function call returns one `chrono::nanoseconds`; a host
sequence returns a duration vector in the same order as its input names. The
measurement covers resolution, compile-time execution, report construction,
and post-step verification, but not the sequence's one-time input snapshot.

Durations are a separate C++ output. They never enter `Attr`, canonical module
text, or the CLI, so enabling measurement cannot make a report
nondeterministic. The unmeasured overload neither allocates a duration vector
nor reads the clock. Failure clears the duration output while the existing
outer transaction restores the module. A regression runs the same sequence
with and without measurement and requires structurally identical IR and
reports.

## M9 local distribution slice

The module directory is now executable tooling rather than a prose convention.
`joggle module list/info/check/install/upgrade/uninstall` discovers explicit
roots, loads full dependency closures, and reports the exact source and native
files selected by path precedence. There is still no registry, generated
manifest, or process-global search path.

Installation rejects links and special files, refuses collisions, copies into
a same-filesystem staging root, validates the staged module through `Env`, and
only then renames it into place. Removal parses the selected `module.jog` and
checks its declared name before detaching the exact directory. Upgrade retains
all installed signatures after alpha-normalizing generic names, permits additive
overloads, validates the full replacement in staging, and restores the prior
directory if commit fails. Tests cover a real native module, additive and
incompatible upgrades, failed dependency validation without residue, duplicate
rejection, and a fresh external CMake consumer of the installed library and
modules.

## M10 transport slice

The ONNX codec now exercises the core's general multi-result functions rather
than imposing a single-output subset. It transports any node result count and
any non-empty graph output list, preserves optional result positions, and adds
explicit tensor types from inputs, outputs, intermediate `value_info`, and
initializers. The implementation still has no switch over ONNX operator names:
codec-level structure and type facts cross the boundary, while interpretation
remains the responsibility of an explicit semantic module.

The pinned official MobileNetV2 remains the real-model regression gate. A tiny
in-memory protocol fixture separately covers two-result/two-output structure so
that this compatibility path cannot regress merely because the pinned model is
single-output.

## M10 semantic substrate slice

The first reusable network library remains deliberately small but is no longer
declaration-only. `tensor` supplies storage-neutral indexing, shape-product,
elementwise addition, and matrix multiplication; `nn.linear` and `nn.relu` are
defined through those primitives. Their `.jog` bodies lower abstraction by
ordinary function structure—calls, loops, conditions, and value updates—so a
later transform can inspect or replace any level without switching IRs or
asking core what a neural operator means.

Frontend bridges also need to read schema attributes. Deterministic list/dict
operations now live in `base`: `len`, `keys`, `has`, `get`, and overloaded
indexing. `ir` is correspondingly restricted to IR handles. This permits a
normal function to interpret ONNX or TFLite attributes without per-frontend
core hooks. The same change generalizes compound assignment and makes all
explicitly imported overloads participate together, which is required for a
specialized tensor/format overload to reuse base scalar algebra in its body.

## M10 body-expansion slice

The bridge between abstraction levels is an explicit function-body edit.
`Env::resolve(mod, op)` applies the same import, qualification, overload, and
generic rules as verification. `Env::expand(mod, op, fn)` substitutes that
ordinary body at the call, remaps nested control flow and dataflow, specializes
type and shape parameters, and preserves visible result bindings. The textual
surface is the ordinary pair `ir.resolve` and `ir.expand`.

Network transforms may submit aligned `list<Op>` and `list<Fn>` values to the
same `ir.expand` function. This is not a second lowering primitive: it applies
the scalar edit in list order, records one expansion event per pair, and commits
the complete list atomically. The C++ span overload has identical semantics.
One snapshot and one indexed dominance validation replace a whole-module copy
and repeated linear dominance searches per call, while a failed pair restores
the exact input text, dependencies, and revision.

`opt.expand` adds only caller-selected policy: it exposes one level of the
named callees from a traversal snapshot. It does not recursively expand calls
created during the same invocation. A network may therefore expose
`nn.linear` into a `tensor.matmul` plus bias loop, optimize at that boundary,
then expose `tensor.matmul` into explicit nested loops in a later step.

The workflow gate requires C++ and `.jog` selection to create structurally
identical IR, concrete `M/N/K` specialization to round-trip through text, and
rejected metadata-bearing expansion to leave both bytes and revision intact.
No operator, frontend, target, or schedule name appears in the core mechanism.

## M10 semantic-bridge slice

A frontend codec cannot assume the semantic modules that a later experiment
will choose. The generic `Mod::use`/`ir.use` edit therefore adds a dependency
explicitly and idempotently, with ordinary revision and rollback behavior. It
accepts only a module already loaded by the current environment and never
performs an implicit load.
`ir.uses` exposes the resulting dependency list without introducing a graph or
package object.

For the common same-signature case, `opt.rename` consumes a list of exact
source/destination call-name pairs. An ONNX-like ReLU fragment is tested by
adding `nn`, mapping `onnx.Relu` to `nn.relu`, resolving the result through the
normal type system, printing, and reparsing. The helper has no frontend table;
the bridge function owns the relation. More involved schema differences remain
normal module code using attribute access and IR construction.

## M10 type-algebra slice

Network and format passes require structure, not parsed type strings.
Compile-time `Ty` values now use the same immutable tree as the verifier:
`ir.type` reads a value type; ordinary `name`, `args`, `int`, `str`, and `ty`
functions decompose, project, serialize, and construct it. The overloaded
`ir.type(m, value, type)` and `Mod::type` write a type through a structured
carried-value family and participate in revision tracking and rollback.
`ir.returns(m, fn, types)` supplies the corresponding function-result edit;
the normal verifier then checks all nested returns and resolved callers before
the surrounding transform commits.

A tested `.jog` function constructs `tensor<f32, [2, 3]>` from child `Ty`
values, annotates an open frontend result, then computes six elements by
walking the resulting tree. A direct C++ edit produces structurally identical
IR; invalid types leave bytes and revision unchanged. This is the substrate
for later shape, custom-bitwidth, and layout inference modules without adding
those policies to core.

## M10 network-transport slice

ONNX node attributes are source facts, not tensors flowing into an operator.
The codec now places the complete node dictionary under an open `onnx`
operation attribute and leaves only declared node inputs on the call. It still
does not inspect operator names. On the pinned official MobileNetV2, every
`onnx.Relu` consequently has its real one-argument signature and the existing
generic bridge maps the complete set to `nn.relu`; verification, canonical
round-trip, and a no-change second run are required.

Attribute list projection is likewise representation-neutral. Dictionary
indexing, `get`, and `ir.meta` materialize stored lists as ordinary compile-time
lists, so a module can iterate schema dimensions directly. Explicitly typed
empty lists retain their annotation during inference. These two rules let the
`tensor` module implement `elem`, `shape`, and `type` as normal `.jog`
functions, with no tensor case in the evaluator and no string parsing in the
module.

## M10 dynamic-shape transport slice

Dynamic shape transport reuses structural terms. `_` is an open term even when
nested inside a list, so `tensor<f32, [_, 3]>` satisfies the tensor
constructor's `list<int>` constraint without creating a dynamic tensor class.
Named extents use the existing stronger form: an integer function generic such
as `N` may occur in every tensor type that shares that dimension.

The ONNX codec collects `dim_param` identities across the graph and emits them
as ordered `int` generics on the imported function. One collision-safe mapping
is shared with value naming, preventing a tensor binding from shadowing a
dimension. The optional-module test builds a binary model containing two
colliding symbolic spellings plus an anonymous extent, calls the real native
reader, verifies `tensor<f32, [batch_size, batch_size_1, _]>`, and requires a
canonical round-trip. TFLite `shape_signature` already maps negative extents to
the same `_` term. This slice preserves named identity and anonymous openness;
it does not ask static-only network transforms to guess a runtime extent.

## M10 value-semantics slice

The open attribute mechanism now covers `Val` as well as `Fn` and `Op`.
Inline binding syntax keeps ownership visible: `[place: "edge"]` before a
statement annotates its computation, while
`let [quant: {...}] y: tensor<...> = f(x)` annotates the resulting data. The
same inline form applies to parameters and generic bindings. C++ and `.jog`
reflection use symmetric `meta`, `has`, `set`, and `unset` operations.

Metadata on a mutable source binding is copied through its internal loop and
branch versions, and editing any member updates the full carried-value family.
Deep clone copies value metadata, region fusion preserves the live-out, and
body expansion merges caller-visible result metadata with the exposed body,
rejecting a conflicting key. A value without a printable source binding cannot
be annotated through the editor, so canonical printing remains complete.

The ONNX codec uses value metadata to retain original value names after safe
identifier normalization. The TFLite codec separates tensor descriptors from
operator options: tensor index, original name, buffer identity, and optional
quantization, sparsity, or variable state belong to `Val`; reflected builtin
options belong to `Op`. A generated quantized-Add model verifies that these
descriptors survive parsing and printing. The generic floating-point bridge
now recognizes nonempty scale/zero-point metadata and retains such a call;
plain integer expansion would not represent the required rescaling.

## M10 network-semantics slice

Some useful function generics occur only in the result. Overload resolution now
uses an explicit result annotation as an additional structural constraint after
matching inputs. The same binding is observed by verification,
`Env::resolve`, and body expansion. Concrete result mismatches still select the
input-compatible declaration and produce the existing precise diagnostic;
result types are not a separate overloading axis.

Mixed scalar/index arithmetic preserves the left operand type, allowing normal
loop-index address expressions while leaving more specific module overloads in
control.

This permits `nn.conv2d` to state one grouped NCHW computation as an ordinary
function body. Its stride, padding, dilation, and group are values, while input,
weight, and output dimensions are structural generics. The body uses the same
loops, conditions, tensor indexing, and scalar operators as user code.
`nn.global_avg_pool2d`, `nn.batch_norm`, and `tensor.reshape` use the same
representation for spatial reduction, channel normalization, and shape change.
Only square root remains a named scalar primitive in the narrow `math` module.
Tests expand each body, verify the resulting nested structure, and round-trip
it.

Average and maximum 2-D pooling share one explicit
`kernel/stride/pad/dilation/axes` convention. Their ordinary bodies cover NCHW
and NHWC without a layout enum; short overloads preserve the earlier unit-
dilation spelling. ONNX AveragePool/MaxPool and TFLite AveragePool/MaxPool
materialize their schema fields into these same calls before body expansion.

Broadcasting now also feeds `nn.sub` and `nn.mul`. Their function bodies reuse
the same `tensor.broadcast` relation as Add and finish in ordinary tensor `-`
and `*` loops. ONNX and unquantized TFLite binary calls therefore differ only
in their explicit bridge metadata and optional fused activation, not in their
shared computation.

The optional `onnx.nn` module owns the frontend/library relationship. Its
`infer` function propagates supported tensor facts to a revision-checked,
graph-bounded fixed point;
its separately invoked `convert` function materializes Conv attributes as
ordinary operands and maps Conv, BatchNormalization, ReLU, Add/Sub/Mul,
AveragePool, MaxPool, GlobalAveragePool, Reshape, Flatten, and rank-two-or-higher
MatMul to shared semantics. Flatten and MatMul become existing tensor functions
rather than parallel NN declarations. MatMul's generic body broadcasts leading
batch dimensions and computes source offsets explicitly; the more specific 2-D
overload preserves the compact three-loop form. Transpose similarly
materializes ONNX's permutation attribute as an operand of the rank-generic
`tensor.permute` body. The vector cases of ONNX MatMul remain open.
The codec remains
name-agnostic, unknown calls remain open, and neither action happens on load.
The official model gate requires every intermediate node result to become
typed, every compute node to leave the ONNX namespace, conversion to verify and
round-trip, and repeated inference and conversion to be textually unchanged.
Conversion discards the `onnx` metadata only after materializing its semantic
values, making the boundary explicit instead of teaching expansion how to
reinterpret frontend provenance. A second official-model gate expands all 155
compute calls from one pre-edit snapshot, verifies the resulting nested loop
IR, and round-trips it structurally.

Tensor inspection has two explicit predicates. `tensor.valid` checks only the
constructor tree, while `tensor.static` also requires integer-literal extents.
Network relations that perform shape arithmetic first require `static`; an
open result or a symbolic extent remains a legal source call. This policy is
also exercised by an imported symbolic-shape BERT graph, preventing partial
inference from becoming a compiler error.

## M10 second-frontend slice

The optional `tflite` module validates that the frontend boundary is not an
ONNX-shaped accident. It uses TensorFlow's version-3b FlatBuffer schema and
generates a mini-reflection header only in the build tree. One generic reader
therefore transports every option table known to that pinned schema; it never
switches on operator names, and FlatBuffers remains absent from the core and
public header.

The pinned official TensorFlow Hub MobileNetV2 has one NHWC input, one output,
107 buffer-backed tensors, and 66 compute calls: 36 Conv2D, 17 depthwise Conv2D,
10 Add, and one each of average pool, reshape, and softmax. The gate requires
binary verification, typed import, exact option dictionaries, payload
preservation, canonical round-trip, and rejection of truncated input. Semantic
conversion is deliberately not guessed by the codec: TFLite's NHWC layouts,
fused activations, bias operands, and quantization metadata must become an
explicit `tflite.nn` relationship rather than hidden reader policy.

## M10 layout-relation slice

Layout is expressed as data at the network/tensor boundary rather than as a
new IR kind. A logical-axis list labels each physical dimension; for example,
NHWC is `[0, 2, 3, 1]`. The value `-1` marks a fixed singleton dimension, which
lets the same convolution body cover TFLite's `[1,H,W,O]` depthwise weights.
The ordinary `tensor.extent`, `tensor.offset`, and `tensor.coord` functions are
the complete interpretation mechanism. A research module may construct a new
axis list without registering a layout or changing core.

The pure `.jog` `tflite.nn` relation materializes axis lists and schema options
as operands, then converts all 66 MobileNetV2 compute calls to `nn.conv2d`,
`nn.add`, `nn.avg_pool2d`, `tensor.reshape`, and `nn.softmax`. Standard and
depthwise convolution differ only in weight axes and group count; bias and
fused activation are ordinary function composition. Conversion is explicit,
idempotent, and removes source metadata only after its meaning is represented.
The gate resolves and exposes one body for every converted call, verifies the
nested IR, and requires a canonical structural round-trip.

The same relation maps both TFLite average and maximum pooling to shared `nn`
bodies. The pinned model exercises average pooling; a focused compiler test
covers MaxPool conversion and expansion without treating the fixture as a
model-quality or performance benchmark.

Before any TFLite conversion, the relation checks all input and output `Val`s
for nonempty scale and zero-point vectors. Quantized calls stay in the TFLite
namespace until a frontend relation can prove and materialize their rescaling
through `quant`. Empty or min/max-only FlatBuffer quantization tables do not
misclassify floating-point models.

## M10 broadcast slice

Broadcasting is shared computation rather than frontend policy. The ordinary
`tensor.broadcast_shape` and `tensor.broadcastable` functions define
right-aligned rank extension and singleton dimensions. `broadcast_offset`
maps one output linear index to its source index, and `broadcast` exposes that
mapping as a normal loop body. None is an intrinsic or a new operation kind.

`nn.add` accepts two independently shaped tensors and materializes both through
that relation before ordinary elementwise addition and activation. The ONNX
bridge uses `broadcast_shape` for type propagation; both ONNX and TFLite
bridges require each input shape to be broadcastable to the declared result
before conversion. An incompatible source call therefore remains visible
instead of acquiring guessed semantics. The dedicated network gate covers a
`[1,3,1] + [2,1,4] -> [2,3,4]` network, expands the two broadcast bodies to
loops, and round-trips the result.

Coverage is queried rather than declared. `opt.unresolved` walks ordinary calls,
uses the normal resolver, and returns each unresolved callee once in structural
order. The TFLite gate observes six source families before conversion and none
afterward; the same query works for a target module without knowing either
frontend.

## M10 symbolic network slice

Tensor shape relations now have two deliberately different projections.
`tensor.shape` returns concrete `list<int>` extents for arithmetic that really
requires integers. `tensor.dims` returns structural `list<Ty>` terms and is used
by relations that only need equality, singleton broadcasting, permutation, or
a directly representable partition product. This is enough to preserve a
generic batch dimension through common network paths without embedding a shape
solver or expression AST in the core.

Function expansion can materialize a generic `list<int>` containing integer
generics owned by its caller. As a result, symbolic reshape, matrix
multiplication, and permutation bodies remain ordinary nested `Fn/Blk/Op/Val`
IR after exposure. A relation that would require multiplying two symbols or a
symbol by a non-unit coefficient returns `_` and leaves its source call at the
frontier. The boundary is conservative and testable rather than model-specific.

## M10 partial-shape network slice

Spatial operators no longer require every tensor dimension to be a literal.
The ONNX relation projects raw dimension terms, checks only the kernel and
spatial extents used in integer arithmetic, and carries batch/channel terms
unchanged. Its Conv result relation is shared by inference and conversion, so a
standalone conversion cannot accept an output shape that inference would have
rejected. Group/channel consistency, optional bias shape, strides, dilations,
and padding mode are checked before any edit.

QuantizeLinear and DequantizeLinear first propagate shape and element type in
source order. The separate `quant` module then defines zero-point subtraction,
scale multiplication, round-to-nearest-even, saturation, and per-axis parameter
selection in ordinary function bodies. ONNX conversion supplies the axis and
numeric bounds as values only after input, scale, zero-point, and result types
agree. This lets QDQ networks expose their quantized boundary without putting
an ONNX field, integer width, or target policy in core.

Across the local QDQ ResNet50, MobileNetV2, and EfficientNet-Lite4 models, all
258 QuantizeLinear calls and 652 of 653 DequantizeLinear calls map to the same
two functions. The one retained call has a scalar imported result that
conflicts with its matrix-shaped input, demonstrating that refinement and
conversion do not overwrite a bad interface fact merely to improve coverage.

## M10 axis-reduction slice

`tensor.line_offset(shape, axis, line, item)` maps a line orthogonal to any
tensor axis back to a linear element offset. The shared `nn.softmax` body uses
that one relation for its maximum, exponential sum, and normalization loops, so
it is neither last-axis-only nor rank-specific. TFLite materializes its fixed
last-axis convention, while the ONNX relation normalizes an explicit positive
or negative axis. An omitted ONNX axis remains open because the schema default
changed across opsets and guessing it would silently alter a model.

`tensor.reduce_offset(shape, axes, line, item)` generalizes that coordinate
split to a set of unique axes. `tensor.mean` uses it in one ordinary function
body; `tensor.reduced` separately derives the structural result shape for
retained or removed dimensions. ONNX negative-axis normalization and
`keepdims` interpretation stay in `onnx.nn`, while the reusable computation has
no frontend fields. Duplicate and out-of-range axes are rejected before any IR
edit.

The neighboring point-algebra slice deliberately does not add LayerNorm or
GELU records. ONNX `Div` and `Pow` reuse broadcast-aware `nn` functions, while
`Sqrt`, `Reciprocal`, `Tanh`, `Exp`, `Sigmoid`, `Ceil`, and `Round` map to
elementwise bodies over ordinary scalar math declarations. The same-signature
subset is held in one source/destination relation table used by both inference
and conversion. A complete typed normalization chain can therefore be
inferred, converted, and exposed one function at a time. Plain ONNX
Add/Sub/Mul select two-operand overloads; the activation operand belongs only
to frontend operations that encode a fused activation.

## M10 shape-program slice

Shape computation remains ordinary dataflow. The textual reflection boundary
now exposes `ir.def(value)`, matching the existing C++ `Val::def`, and checked
byte size/index queries let a module inspect compact constants without a native
callback or container ABI. `onnx.nn.shape_terms` recursively interprets only
shape-producing calls: tensor constants, Shape, axis-zero Gather and Concat,
one-dimensional Slice, Unsqueeze/Squeeze, Identity, and Cast. It returns
structural `Ty` terms, so a dimension such as `N` is never flattened into a
string or frozen to an integer. When a shape scalar is computed but not
representable as one existing term, the evaluator retains one `_` extent
rather than discarding the known rank.

`reshape_result` applies ONNX zero and inferred-dimension rules conservatively,
then conversion requires the existing result type to equal that relation. The
test builds `[N, 12]` through a realistic
Shape/Gather/Slice/Unsqueeze/Concat/Cast chain before exposing
`tensor.reshape`. Exact factors such as `[N, 256] / [256]` recover `N` through
the reusable `tensor.quotient` relation. Unsupported shape programs simply
retain partial dimensions or leave the source call intact; there is no durable
shape dialect, metadata cache, or core operator switch.

## M10 transformer-network slice

Tensor refinement merges only compatible holes, so inferred structure cannot
overwrite an imported interface contract. Shape-of-shape relations now cover
Gather, Slice, Squeeze/Unsqueeze, Concat, ConstantOfShape, and Split. OneHot,
DynamicQuantizeLinear, MatMulInteger, and the transposed-batched shape of
`com.microsoft.FusedMatMul` propagate types before semantic conversion.

Call conversion now uses the general `ir.retarget` edit. It checks a proposed
callee and operand list with normal module visibility and overload resolution,
then commits both together; a mismatch changes nothing. This removes the gap
between a bridge's local shape checks and final module verification. It also
exposed and fixed empty type-list classification, allowing scalar tensor shape
`[]` to bind an ordinary `list<int>` generic.

On the locally imported 12-layer quantized BERT graph, inference leaves no open
result binding. Conversion maps all 844 covered floating tensor operations:
70 Reshape, 50 ReduceMean, 49 Transpose, 185 Add, 62 Sub, 342 Mul, and the
remaining Pow, Sqrt, Reciprocal, Tanh, and Softmax calls. The quantized path,
Cast, and scaled/transposed MatMul add 72 dynamic quantizers, 84 integer
matrix multiplications, 150 element conversions, and 12 scaled matrix
multiplications through generic `quant` and `tensor` bodies. Cast conversion is
deferred until shape consumers have read their source programs.

The next phase gives runtime shape transport ordinary tensor semantics rather
than inventing a shape IR. `tensor.shape`, `gather`, positive-step `slice`,
`one_hot`, and `fill` are inspectable functions. Binary `tensor.concat` is the
single composition primitive: the bridge folds any source arity into a chain,
and expresses every Split result as a slice. `ir.name` completes the editing
symmetry needed to keep source-readable result names, while arbitrary tags on
the decomposed operation are copied without treating `host`, `place`, or
`schedule` specially. On BERT this converts 55 Concats into 133 binary calls,
5 Shapes, Gather, 5 Slices, the 2-result Split, OneHot, and ConstantOfShape.
Two Squeezes and one Identity retain conflicting imported symbolic contracts
and deliberately stay explicit. Both inference and conversion remain
byte-idempotent.

## M10 schema-version slice

Frontend compatibility is data, not a version suffix in a function name.
`onnx.opset(m, domain)` reads the imported `onnx.model` descriptor through
ordinary `ir` and `Attr` operations. The first consumer is Softmax: the
[pre-13 schema](https://onnx.ai/onnx/operators/onnx__Softmax.html) flattens the
dimensions from `axis` onward, whereas version 13 and later normalize one axis.
The shared `nn.softmax` axis-list overload expresses the former directly with
`tensor.reduce_offset`; its scalar-axis overload delegates to the same body.
The bridge therefore chooses an explicit axis list from the model's opset and
refuses to infer a missing version. A rank-three regression distinguishes the
two semantics, and imported opset-12 BERT retains all 12 Softmax conversions.

## M10 subgraph-signature slice

Nested frontend graphs do not justify another IR object. `ir.find` performs
exact local `Fn` lookup, while `ir.params` and `ir.returns` expose the complete
signature. ONNX transport metadata records only a body function name, its
formal input count, and capture operand positions; `onnx.graph` decodes that
record back to the ordinary function handle. The core remains unaware of ONNX
and of control-flow operator names.

The first consumer is Loop interface inference. The relation treats the first
two body parameters and first body result as the ONNX iteration/condition
protocol, propagates explicit loop-carried operands into child parameters, maps the
remaining leading body results back to loop-carried outputs, and prepends one
unknown trip extent to scan results. Lexical captures use the same checked
parent-operand-to-parameter mapping. A malformed interface remains unchanged,
and capture validation completes before mutation. The in-memory test starts
with untyped carried and captured parameters, then checks both are refined.
On the pinned official Tiny-YOLOv3-11 model the relation closes all eight Loop
results and unlocks dependent Reshapes, reducing the observable frontier from
280 to 219. CI pins that partial frontier: it can improve deliberately, but
cannot
silently regress or be reported as full model support.

## M10 portable-C slice

The target-neutral CLI path now writes a read-only module function's `str` or
`bytes` result verbatim. The `c` module is its first execution consumer and is
implemented entirely in `.jog`: it traverses the same `Fn`/`Blk`/`Op`/`Val`
structure, emits local scalar calls and structured branches/loops, flattens
static tensor indices, and uses caller-provided storage for tensor results.
Scalar type spelling and byte width come from one ordinary ABI dictionary
owned by the module; fixed C operator spellings are likewise module data, not
core cases. The dictionary contains only real IR scalar types. Semantic
indices use their signed scalar ABI, while emitter-created fixed-array loops
use C's `size_t` without inventing an IR pseudo-type. The emitter
also recognizes optional `mem.slot`
metadata; no C-specific field or storage object was added to core IR.

Qualified external symbols use a readable module separator in C
(`edge.matmul` becomes `jog_edge_matmul`). The same prototype pass rejects
collisions after normalization. Calls mapped to standard C operators or libm
functions are not also reported as external Joggle dependencies.

The companion `c.header` function reuses the source emitter's checked
prototypes and returns a C/C++-compatible header. Header generation is not an
artifact kind in the host or core: it is another ordinary `fn(Mod) -> str`.

Emission is deliberately closed over the exposed computation. Dynamic tensor
shapes, multi-results, and calls whose bodies still live in a dependency fail
with diagnostics; the emitter does not perform hidden lowering or invent
semantics. The regression gate emits a concrete matrix multiplication and
scalar call/branch functions, compiles the result as C99 with warnings treated
as errors, executes it, and compares the numerical outputs. This establishes
one portable end-to-end target while leaving dynamic allocation,
inter-function planning, and a genuinely different second target open.

The companion `c.prepare` transform is explicit and uses the emitter module's
same structural support predicate. At a bounded fixed point it expands only an
unsupported call's already-defined ordinary body; missing semantics and
unhandled operation metadata are errors. A shared `tensor` addition therefore
becomes its existing tensor constructor, shape-list loop, element loop, loads,
scalar addition, and stores. A second execution gate compiles and runs that
path, while direct emission of the unprepared call continues to fail. This
keeps preparation inspectable and separate from read-only artifact generation.

Application-scale exposure adds many static helper calls and assignment copies.
Those are handled before target legalization by two ordinary `opt` functions:
`fold` executes explicitly selected `Fn` handles on static operands, and `copy`
performs batch identity propagation. Their commit path replaces values and
erases operations in whole batches, so use lists and dominance are not rescanned
once per selected call. The official expanded MobileNetV2 model now completes
`c.prepare`, static storage planning, C99 emission, strict compilation, and
comparison of all 1,000 official outputs. Logical `&&` and `||` values remain
structured branches in the IR and
are recovered from their forwarding arm by the C module; the core has no C
expression case.

## M10 deterministic-VM slice

The second target starts as a closed scalar path rather than another emitter
facade. The pure `.jog` `vm.image` function obtains constant-time, current-module
value identities through `ir.key`, rejects unsupported structure, and emits a
textual image. A key is never persisted as model semantics and is not stable
across printing or reparsing. The matching native `vm.run` function decodes one
selected entry into typed instructions, compact register slots, and precomputed
structured-control bounds before executing byte inputs. The image protocol and
instruction meanings belong entirely to the module; core contains no VM
operation or image format.

Image version 3 covers signed 64-bit arithmetic, `f32` and `f64` arithmetic and
conversion, the current ten-function floating `math` surface, Boolean values,
comparisons, bitwise
operations, structured branches and range loops, scalar literal-list
selection, and static tensors of those elements. Every image
value carries an explicit primitive format. Integer and floating inputs retain
their 8- or 4-byte widths; tensor parameters and results use row-major elements.
Allocation, fill, multidimensional load, and versioned in-place update remain
explicit image instructions. A tensor-literal instruction embeds the canonical
hex payload produced by `tensor.literal`, validates its native byte width, and
does not introduce an internal core tensor representation. Loop-carried scalar
and tensor values retain the ordinary IR semantics. Arithmetic right shift is
defined from unsigned bit operations rather than a host implementation-defined
signed shift. Execution reports deterministic instruction steps, not hardware
cycles. Malformed
images, mismatched arguments, invalid shifts, integer division by zero,
out-of-range casts, and invalid tensor accesses are negative gates. Repeated
image generation and execution must be identical.

The VM and C regressions evaluate `abs`, `floor`, `log`, `erf`, `sqrt`, `exp`,
`ceil`, `pow`, `tanh`, and ties-to-even rounding from the same semantic
declarations. Both reference
targets currently delegate transcendentals to the host standard library, so
the deterministic claim applies to image text, control flow, and instruction
counts—not cross-platform last-bit equality. A fixed approximation or LUT is a
module-selected implementation and must be evaluated as such.

The same `i64` tensor addition and both `i64` and `f32` nested-loop matrix
multiplications are now executed by the C and VM targets. The pinned ONNX
v1.19.0 backend `test_matmul_2d` case additionally exercises the complete
binary-import, semantic-conversion, dead-data cleanup, body-expansion, VM, and
compiled-C path against its official TensorProto output. The slice remains
intentionally incomplete: the full second-target gate still requires a
module-defined format path. Application-sized imported execution is now a
closed numerical gate: the pinned official MobileNetV2 input produces all
1,000 expected outputs through both VM and generated C.
The high-level `tensor.operator +` path is separate from those handwritten
loops: both parameterized `opt.expand` and the explicit `vm.prepare` function
expose and execute its shared body. `vm.prepare` supplies `vm.accepts(Mod, Op)`
to the same generic `opt.expose` library used by C; image emission stays
read-only and performs no hidden lowering. A mismatched argument type and a
mutating predicate are rollback gates. The VM parser now interns textual
registers once, validates and decodes every selected instruction once, and
matches branch and loop boundaries in one structural pass; execution uses
compact slots rather than string parsing or register hash lookup. On an Apple
M4 Release build, the exposed MobileNetV2 image contains 26,017 lines and
28,794,453 bytes. Its official input completes in 399.674 seconds and exactly
98,167,456,513 VM steps, after which every output passes the same tolerance as
compiled C. This closes correctness at application scale while exposing the
real structural bottleneck: eager scalar exposure inflates work by orders of
magnitude, so future performance work belongs in retained computation and loop
transformation rather than frontend- or operator-specific VM cases.

### Storage planning

The pure `.jog` `mem` module demonstrates that resource policy can live above
emitters without becoming a device model. `mem.plan` walks ordinary
`Fn`/`Blk`/`Op`/`Val` dataflow, groups the value versions of each local tensor
binding, derives a conservative live interval, and assigns the first compatible
free slot. Slots never mix element types and grow to the largest required
static extent. Parameters and constants remain outside the plan.

Structured control flow carries lexical environment values through loop and
branch boundaries. Those container and `yield` edges are aliases, not memory
reads; nested operations remain the source of true uses. Treating the carrier
edges as uses would keep every prior tensor live until the last loop. The
planner therefore ignores only those structural edges while retaining loads,
stores, calls, and returns. The resulting `mem.slot` values plus function-level
`mem.types` and `mem.counts` are ordinary open metadata, so another allocator,
simulator, or emitter can replace or consume them without a core API change.

The regression starts from three composed `tensor` additions, explicitly
exposes their shared bodies, assigns three logical intermediates to two
buffers, repeats the plan to prove byte-level idempotence, emits C99, compiles
with warnings as errors, and executes the numerical result. Running `c.source`
without `mem.plan` retains the earlier one-array-per-binding behavior.

## M11 structural measurement slice

The pure `.jog` `stat` module is the first research-instrumentation slice.
`stat.summary` derives a canonical dictionary directly from the public
reflection surface: function, block, operation, and value counts;
operation-kind counts;
distinct callees and unresolved calls; tensor values and their known static
elements; and optional `mem` slot counts and capacities. The implementation
contains no frontend operator or target name.

The distinction between structural and physical counts is explicit.
`static_tensor_elems` counts elements represented by static tensor `Val`s,
including control-flow versions, whereas `mem_elems` counts capacity in an
explicit storage plan. Neither field predicts bytes, latency, or energy because
those require a module-owned data-format or device policy. Canonical dictionary
ordering makes the output diffable and suitable for experiment records without
putting a report class in core. A regression fixes the complete summary for the
shared matrix example and the memory execution gate checks its two-slot,
eight-element plan.

`stat.sum(m, measure)` demonstrates the open measurement boundary rather than
defining a built-in cost model. It traverses operations and invokes an ordinary
module function of type `fn(Mod, Op) -> int` through `ir.invoke<int>`, rejecting
mutating measures by revision. A research module can therefore assign cycles,
energy proxies, code-size weights, or resource units without a device class or
new core intrinsic. The unit and model remain explicit experiment policy.
