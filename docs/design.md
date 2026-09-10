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

## Invariants

- Core depends only on the C++20 standard library.
- Adding computation never adds an `Op` subclass or parser case.
- Adding a module never generates or recompiles a core header.
- Mutations go through `Mod` and preserve handle/use-def integrity.
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

The first external frontend target is the official ONNX Model Zoo
`mobilenetv2-7` model mirrored by the ONNX organization on Hugging Face. The
test input is pinned to repository revision
`b055c14ebe95ca2df473547484e4d447867951ed`; its 14,246,826-byte model has
SHA-256 `c1c513582d56afceff8516c73804e484c81c6a830712ab6d682253f4a3cd042f`.
`test/model.cmake` downloads and verifies it only when explicitly invoked, so a
normal build remains offline.

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
The same codec boundary is now independently exercised by the TFLite module.

The pinned model now passes the complete codec gate: binary decode, generation
of 267 tensor constants and 155 calls, parse, verify, canonical print, reparse,
and structural equality. Its 14,156,560 bytes of initializer payload are
preserved in typed tensor constants. A Release run on the M1 reference machine
imports and prints the self-contained 28.4 MB module in approximately 0.49 s;
the second parse-print takes approximately 0.31 s. These are local regression
measurements, not general performance claims.

## M4 slice

The optional `sat` module is the extension-boundary gate. Its `sat<W>` type and
`sat.add` primitive are ordinary signatures. Its textual `sat.select` function
inspects the structural type and explicitly converts it to text for the native
predicate; integer additions remain untouched. Three native scalar functions
recognize supported formats, execute the saturating reference semantics, and
emit a concrete SystemVerilog adder.

SystemVerilog is an output of that removable module, not a core backend or IR.
After structural type reflection completed the boundary, the whole format,
policy, simulator, and emitter were added without changing the core
library, parser, evaluator, public header, or operation vocabulary.

## M5 slice

M5 turns open metadata and reflection into construction rather than mere
inspection. A module can use arbitrary function metadata to select work, query
users, insert any call, replace uses, and erase the old operation. Dynamic list
literals allow the same source language to collect IR handles.

The operator-neutral region primitive computes live-ins and a single live-out
for an ordered call region, enforces dominance and motion safety, preserves the
visible result name, and commits the fusion atomically. The generic textual
`opt.fuse` helper follows a user-supplied callee sequence; it contains no ONNX
or NN operator names. A test-only bridge applies the helper to the pinned
MobileNetV2 import and replaces 36 Conv-BatchNormalization-ReLU chains with 36
user-named calls, reducing those 108 calls to 36. The optimized 28.4 MB module
then verifies, prints, reparses, and remains structurally equal.

## M6 first slice

`Ty` now preserves canonical text while exposing a recursive constructor tree.
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

The generic `sat.add<W: int>` declaration is the first module-defined
parametric gate: inferred and explicit widths succeed, conflicting widths,
wrong parameter types, and wrong arity fail with located diagnostics, and no
saturating-arithmetic case exists in the core. Language normalizations
(`base.copy`, list construction, and indexing) retain only the minimal
intrinsic rules needed to recover ordinary source bindings and iteration.

## M7 first slice

A call now has zero or more ordered results in both the public C++ editor and
textual reflection boundary. The five-argument `ir.call` name is overloaded by
its last parameter: `str` preserves the concise one-result form, while
`list<str>` returns the created `Op`. Results remain ordinary `Val`s and are
named with the overloaded `ir.rename`; no tuple operation or result wrapper was
added.

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
moves operations within a `Blk` through explicit `Op` positions. Deep cloning
creates fresh results, `Blk`s, and `Blk` arguments, remaps internal dataflow,
and works for nested loops and conditions; erasing the replaced source
recursively invalidates its complete subtree. Motion is atomic and checks the
whole module's dominance before commit. The identical operations are available
through `ir.constant`, `ir.clone`, and `ir.move`, with `ir.kind` and
`ir.blks(op)` completing structural discovery. Named constants now remain
named when printed instead of being silently duplicated as inline literals.

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
The command line exposes this exact report with `--report <file>`. It keeps
canonical `Mod` output on stdout and uses the same public `print(Attr)` overload
as embedding code, preserving one reporting representation.

Compile-time entry points are now checked against the promised
`fn(Mod) -> bool` contract before execution. A false return still means “ran
successfully but reported no change”; malformed entry signatures and runtime
failures remain failures and roll the module back.

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

## M8 fourth slice

Embedding code can assemble an ordered transform sequence by passing a
`span<string_view>` to `run`. This is an overload, not a pipeline object: each
name still selects an ordinary `fn(Mod) -> bool`, and source code composes the
same functions by calling them normally. The sequence reports its constituent
function reports as structural `Attr` values and derives aggregate change/edit
information from the module revision.

The outer execution is transactional. If a later function is missing, has the
wrong signature, fails, or leaves invalid IR, all earlier edits in that
sequence are rolled back together. The workflow test runs the same two
functions once through the C++ sequence overload and once through a textual
wrapper, then requires byte-identical canonical IR; a deliberately bad second
step exercises whole-sequence rollback.

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

The same explicit `Fn` set can supply implementations. `ir.match` performs the
ordinary overload ranking inside that set, while the environment-aware
`ir.expand` permits a function whose local name equals the source call's
resolved symbol. It adds the implementation module only when not already
visible, then specializes and copies the normal function body. Dependency and
body edits roll back together. `opt.apply` is merely the bounded fixed-point
policy over these primitives: it skips bodyless declarations and
metadata-bearing calls, rejects ambiguous or non-converging implementation
sets, and contains no target or NN names. The general `base.assert` primitive
turns bound exhaustion into a located transactional failure. A network
regression chooses a shape-specialized i8 ReLU implementation over a generic
i8 overload, follows a second implementation layer at another shape, and
proves a recursive implementation restores the exact input and revision. Its
ordinary execution report also proves all three selected expansions, including
both overload signatures and the intermediate implementation layer.

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
generic rules as verification. `Mod::expand(op, fn)` substitutes that ordinary
body at the call, remaps nested control flow and dataflow, specializes type and
shape parameters, and preserves visible result bindings. The textual surface
is the ordinary pair `ir.resolve` and `ir.expand`.

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
explicitly and idempotently, with ordinary revision and rollback behavior.
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
`infer` function propagates the supported MobileNetV2 shapes in graph order;
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
`Sqrt`, `Reciprocal`, and `Tanh` map to elementwise bodies over ordinary scalar
math declarations. A complete typed normalization chain can therefore be
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
