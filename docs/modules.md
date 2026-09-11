# Modules

A module is the only extension and distribution unit. A module directory is:

```text
example/
  module.jog
  lib/*.jog
  native/joggle_example.*   # optional
  test/*
```

The directory is a distribution form, not a second IR object. Loading its
sources produces ordinary declarations visible in an `Env`; parsing a model
produces an ordinary `Mod`.

Pure `.jog` modules need no compiler toolchain. Native modules have one stable C
entry point and attach callbacks to body-less function declarations. C++ STL
containers, exceptions, RTTI, and virtual tables do not cross that boundary.

```jog
module sample

[role: "example"]
fn ping(x: i32) -> i32;
```

```cpp
JOGGLE_MODULE_EXPORT bool joggle_module(const jog_api* api,
                                        jog_module* module) {
  return joggle::compatible(api) &&
         api->bind(module, "sample.ping", ping, nullptr);
}
```

`role` above is ordinary module-defined metadata and may be omitted. The native
binding itself requires only a matching external declaration. This keeps model
primitives, native implementations, and textual functions in one function
model.

The callback receives one call frame for arguments, returns, and diagnostics.
The API record carries its ABI version and byte size, hidden behind
`joggle::compatible`; neither the entry symbol nor public C type names contain a
version suffix. Loading rejects bindings outside the declaring module, bindings
to unknown or body-bearing functions, duplicate bindings, missing entries, and
ABI mismatches reported by the module. Calls validate scalar arguments and
returns against the `.jog` declaration. Scalars include length-delimited `str`
and `bytes`; embedded zero bytes are preserved.

The standard modules are deliberately narrow. `base` declares scalar/list/dict
fundamentals, `ir` is universal reflection and editing, `opt` contains reusable
textual transforms, `math` names scalar math primitives, `tensor` defines
storage-neutral tensor computation, `quant` makes quantization policy explicit,
and `nn` contains network semantics. `mem` assigns static tensor lifetimes to
target-neutral reusable slots, while `stat` returns deterministic structural
measurements. `c` is a removable first execution module and an optional
consumer of memory metadata, not a target interface in core. The optional
`onnx` module only transports a binary model. MLIR, JIT, simulation, hardware
description, and additional target experiments remain removable modules.

Version 0.1 searches explicit local paths. The CLI exposes that same local
model directly:

```sh
joggle module list -M modules
joggle module info example -M modules
joggle module check example -M modules
joggle module install path/to/example local-modules -M modules
joggle module upgrade path/to/example local-modules -M modules
joggle module uninstall example local-modules
```

`list` is deterministic across the supplied roots, with the first root taking
precedence for duplicate names. `info` performs a real load, then reports the
selected path, dependencies, source fragments, and native library files.
`check` loads and verifies the full dependency closure.

Installation validates the source tree: symbolic links and special files are
rejected, an existing target is never overwritten, and the copy is loaded from
a same-filesystem staging directory before an atomic rename makes it visible.
Upgrade reads the installed and replacement declarations, alpha-normalizes
generic parameter names, and requires the replacement to retain every existing
function signature. New functions and overloads are compatible. A compatible
replacement is then copied and fully loaded from staging, including its
dependency closure and optional native ABI. Only after validation does the CLI
detach the old directory and commit the replacement; a failed commit restores
the old directory.
Uninstallation first parses the installed declaration and refuses to remove it
when its declared name differs from the requested name. There is no registry
database or generated manifest to become stale. `info`, `check`, and `install`
and `upgrade` load an optional native entry, so native modules are executable
code and must come from a trusted source. Native libraries remain loaded for
the lifetime of an `Env`. Network package resolution, lockfiles, dependency
solving, and in-process hot unloading are out of scope.

The bundled declarations install under `share/joggle/modules`. Applications
choose their module roots explicitly with `Env::path`; the core does not depend
on a process-global environment variable or a compile-time installation path.

Loading a module parses and verifies its declarations after loading
dependencies.
It never runs a transform as a side effect. The caller selects an ordinary
function with `joggle::run` or `joggle run`; this keeps module installation,
function definition, and execution as three separate operations.

Qualified and imported calls resolve through the explicit and transitive `use`
closure. Local and imported declarations form one deterministic visible
overload set, allowing a specialized overload to reuse less-specific imported
algebra in its own body. Merely loading another module into the same `Env` does
not make its declarations visible, and missing `use` edges are diagnosed.
Resolution from a `Fn` handle uses the function's owning `Mod` directly, so a
parsed module need not be installed or loaded under its own name first.
Resolution checks arity and structural types, infers generic arguments, ranks
specificity, and computes result types.
`Env::load` commits the requested module and its transitive dependencies as one
transaction. Failure after a dependency or native library has loaded removes
only state introduced by that request, restores the prior environment epoch,
and retains diagnostics. Previously loaded modules and bindings are untouched;
a failed load therefore does not invalidate reusable query-cache entries.
It is independent of native binding: a declaration may define model semantics,
a textual transform, or a native compile-time service. Unknown calls are
preserved deliberately for frontend transport, but code that needs a
declaration must load or invoke an explicit semantic bridge.

A native binding names an external function family. At invocation, scalar
argument types must select exactly one declaration before the callback runs;
the selected result signature is checked afterward. Thus overload support does
not change the stable C entry or expose C++ containers across the ABI. When
several native overloads cannot be distinguished from the dynamically supplied
scalar values, the call fails as ambiguous instead of choosing by declaration
order.

Type constructors use this same boundary. A module named `format` can export
`fn format<P: Attr>() -> Ty;`; consumers write `use format` and then
`format<...>`. The function's generic list defines arity and ordinary parameter
types constrain compile-time arguments (`int` widths, `Ty` element types, or
`list<int>` shapes). Its `Ty` result identifies it as a constructor without a
second declaration system or kind registry. A qualified constructor name may
target any visible constructor function when the module and type names differ.

The built-in `ir` module is the complete reflection boundary:

| Function | Meaning |
| --- | --- |
| `fns`, `find`, `params`, `returns`, `blks`, `ops`, `uses` | Find and traverse loaded functions, signatures, structure, and dependencies. |
| `args`, `outs`, `def`, `users` | Read operation dataflow in both directions. |
| `live`, `blk`, `kind`, `callee`, `name`, `type` | Query handle state, readable identity, structure, and structural `Ty`. |
| `resolve`, `symbol`, `accepts`, `match` | Resolve calls, identify functions, and select against explicit signatures. |
| `where`, `invoke` | Select functions by open metadata and execute an ordinary `fn(Mod, Op) -> bool` transactionally. |
| `is_const`, `constant` | Query constant IR values. |
| `has`, `meta` | Query open function, value, or operation attributes. |
| `call`, `constant`, `loop`, `branch` | Construct leaves and structured control flow. |
| `clone`, `expand`, `move`, `args` | Copy, substitute a function body, place, or reconnect IR. |
| `retarget` | Atomically change one call and its operands after normal overload resolution. |
| `replace`, `erase`, `rename` | Rewrite dataflow, ownership, and readable names. |
| `set`, `unset` | Edit a function, value, or operation attribute. |
| `use` | Add an idempotent module dependency. |

These functions operate on generic handles and contain no NN operator names.
Adding an importer, optimization, or target module therefore does not extend
the reflection ABI or add a parser case.

The `base.size` and `base.byte` functions provide bounds-checked inspection of
an `Attr` byte payload. This deliberately small primitive is sufficient for a
frontend module to decode compact integer constants; bulk tensor payloads stay
opaque and are never copied into a second core representation.

General compile-time values live in `base`, not `ir`. `len` covers statically
typed lists, dictionaries, and dynamically obtained `Attr` containers;
`keys`, `has`, and `get` expose deterministic dictionary access; and
`attrs["key"]` or `items[index]` is the strict indexing form. `int` and `str`
project a checked attribute leaf when a transform needs a statically typed
value. These are enough for an
explicit bridge function to interpret frontend attributes without adding an
ONNX/TFLite field API or string-key cases to core. A missing strict key is a
diagnostic, while the three-argument `get` supplies a caller-chosen fallback.
`assert(condition, message)` lets any module reject an invalid policy or a
bounded computation that did not converge; failure is located and rolls back
the enclosing compile-time transaction.

Mutable lists and dictionaries also implement `[]=` through `base`. A module
can update a typed work list or assemble a structured report with ordinary
`items[index] = value` syntax; no builder object or report-specific host API is
needed. List updates are bounds checked, while dictionary updates insert or
replace a string key and retain value semantics.

`Ty` is also a normal compile-time value. `name`, `args`, and `int` decompose a
type tree; the `kind` overload distinguishes integer, Boolean, list, and type
terms; `ty` reconstructs one from text, an integer term, or a constructor name
plus child types; `str` is the explicit conversion back to canonical text.
The overloaded `ir.type(m, value, type)` records an inferred type while keeping
loop/condition-carried versions consistent and printable.
Typed empty lists retain their explicit element type, so module functions can
build structural shapes incrementally. Lists retrieved from `Attr`
dictionaries or IR metadata are ordinary iterable compile-time lists; callers
do not need a frontend-specific projection primitive.

### Tensor and network semantics

`tensor` defines `tensor<E, S>`, structural `elem`/`shape`/`dims`/`type` helpers,
linear, two-dimensional, and four-dimensional indexing, `numel`, elementwise
addition, subtraction, multiplication, and matrix multiplication. `valid`
recognizes the structural constructor without projecting it; `static` further
requires integer-literal extents. `shape` deliberately projects only such
concrete shapes to `list<int>`; `dims` retains every extent as a `Ty` term.
The latter lets a relation preserve caller generics such as `N` without adding
a symbolic-expression class. `permutation`, `permuted`, and the inspectable
`permute` body provide rank-generic axis reordering. `product` proves a
partition extent when it is concrete or contains one unscaled symbolic term;
otherwise it returns `_` instead of inventing an expression language.
`quotient` cancels equal symbolic factors and exact integer factors, while
`inserted`, `replaced`, and `gathered` perform reusable structural dimension
edits. `refined` fills only `_` tensor holes and rejects a conflicting element,
rank, or dimension. These relations are ordinary module functions and are
available to any frontend or research transform.
`broadcast_shape` and `broadcastable` are overloaded for concrete shapes and
raw dimension terms. They express trailing-axis compatibility by exact term
equality and singleton expansion, while `broadcast_offset` and `broadcast`
provide the inspectable index and copy semantics for an exposed static body.
`tensor.matmul` keeps a more specific two-dimensional overload and adds one
rank-generic body for operands of rank two or greater. Leading dimensions use
the same broadcast relation; `matmul_offset` maps an output batch coordinate
back into either operand without a layout or attention-specific operation. A
second overload makes last-two-axis transposition and scalar scaling explicit
values while retaining the same batched computation; vendor fused calls do not
become permanent operator families. `tensor.cast` is likewise an ordinary
element loop.
`extent`, `offset`, and `coord` interpret
a physical shape through an explicit logical-axis list. Each physical dimension
names its logical axis; `-1` denotes a fixed singleton dimension. Thus NCHW is
`[0, 1, 2, 3]`, NHWC is `[0, 2, 3, 1]`, and TFLite's depthwise `[1,H,W,O]`
weight is `[-1, 2, 3, 0]`. These are ordinary values, not layout classes or
registered compiler cases. The computations have normal `.jog` bodies with
loops and explicit value updates; they are not opaque operator records.
`quant.quantize` and `quant.dequantize` operate on a generic real element type,
a generic stored element type, and either scalar or one-axis parameter tensors.
The parameter axis, saturation bounds, and round-to-nearest-even primitive are
visible in the function body. The module therefore fixes mathematical behavior
without fixing a bit width, storage class, or target implementation.
`quant.dynamic` returns values, scale, and zero point through the normal
multi-result function model. `quant.matmul` accumulates differing integer input
types into `i32` and accepts scalar, per-row, and per-column zero points.
Runtime tensor structure uses the same model. `tensor.shape`, `tensor.gather`,
`tensor.slice`, `tensor.one_hot`, and `tensor.fill` have inspectable bodies.
`tensor.concat` is deliberately binary: a bridge folds any source arity into a
chain, while a source Split is a set of slices. This small algebra avoids both
variadic operation machinery and declarations specialized to a frontend's
input or result count.
`nn.linear` composes matrix
multiplication with an optional bias loop, while `nn.relu` is a loop and
condition over the same tensor primitives. The general `nn.conv2d` overload
takes three logical-axis lists, so grouped convolution and depthwise
convolution share one loop body across activation and weight layouts. The terse
NCHW overload delegates to it. Bias and fused activation are ordinary composed
functions rather than hidden operator fields. `nn.avg_pool2d`, dilation-aware
`nn.max_pool2d`, broadcast-aware `nn.add`/`nn.sub`/`nn.mul`, and axis-explicit
and axis-list `nn.softmax` overloads provide the remaining shared semantics
needed by the second real-network gate. `tensor.line_offset` enumerates all lines orthogonal
to an axis, while `tensor.reduce_offset` separates ordinary and reduced
coordinates for any unique axis set. The inspectable `tensor.mean` body uses
that relation for single- or multi-axis reduction without a transpose or
rank-specific case; output singleton dimensions are a type relation rather
than a second computation. `nn.global_avg_pool2d` is a normal NCHW
specialization.
The two-operand `nn.add`, `nn.sub`, `nn.mul`, and `nn.div` overloads express
plain broadcasting. Separate three-operand Add/Sub/Mul overloads retain a
frontend's fused activation only when one actually exists. `nn.pow`, `nn.sqrt`,
`nn.recip`, `nn.tanh`, `nn.exp`, `nn.sigmoid`, `nn.ceil`, and `nn.round_even`
expose scalar `math` calls inside their loops, so normalization, GELU,
activation, and shape-derived arithmetic remain visible to later transforms.
The ONNX bridge keeps the same-signature unary subset in one data-driven
source/destination table; adding one relation does not add another inference
branch.
Both spatial pool functions use explicit kernel, stride, pad, dilation, and
logical-axis values, so ONNX NCHW and TFLite NHWC calls share the same bodies.
`nn.batch_norm` exposes inference-time channel
normalization down to scalar algebra and the single `math.sqrt` primitive;
`tensor.reshape` is a linear element copy whose result shape comes from the
annotated call. Transforms can therefore keep a network call
abstract or expose one function body at a time using the same
`Fn/Blk/Op/Val` representation.

`ir.resolve(m, op)` returns the declaration selected by the same structural
overload rules used by verification. `ir.expand(m, op, fn)` then substitutes
that normal function body, including nested loops and conditions. Generic
type, shape, and integer bindings are specialized at the call site; visible
result names and structured carried bindings remain printable. The C++ pair
`env.resolve(mod, op)` and `mod.expand(op, fn)` performs the identical edit.
When a shape generic contains a caller's integer generic, expansion
materializes one ordinary `list<int>` value containing that existing binding.
This keeps bodies such as symbolic reshape, matrix multiplication, and
permutation representable instead of requiring dimensions to be frozen before
body exposure.
`opt.expand` is only a policy helper over an explicit list of callees, not a
built-in lowering stage.

These definitions specify computation but deliberately do not choose layout,
memory space, vector width, tiling, device, or instruction. Such choices belong
to separately loaded research modules and can use open attributes or explicit
function arguments. The ONNX codec does not import `nn`; conversion between a
frontend schema and these functions must remain an explicit user-selected
module function.

The optional `onnx.nn` module is that relationship, not another IR layer.
The transport module's `onnx.opset(m, domain)` query reads the ordinary
`onnx.model` descriptor, giving every relationship module one version source
without versioned function names or parser state.
`onnx.nn.infer` propagates tensor types through quantization boundaries,
convolution, normalization point algebra, broadcast arithmetic, pooling,
matrix operations, tensor rearrangement, and shape dataflow. It repeats a
deterministic source-order sweep until the module revision stops changing and
rejects a relation set that cannot converge within a graph-derived bound.
Quantization nodes contribute only their provable shape and element type here.
Compatible three-input QuantizeLinear and DequantizeLinear calls are then
converted through `quant`; unsupported parameter layouts or element formats
remain source calls.
Nested graph interfaces use the same relation: lexical capture operands and
Loop iteration, condition, and carried operands refine ordinary child `Fn`
parameters. The complete capture map is validated before any type changes. No
graph-specific IR is introduced, so normal relations continue inside the child
function.
Softmax conversion follows the schema boundary explicitly. Before opset 13,
the selected axis begins a flattened suffix, so the bridge supplies that suffix
to the axis-list overload. From opset 13 onward it supplies one axis. A model
without an opset declaration stays at the source boundary rather than adopting
the current schema by accident.
Its structural conversion phase runs only after shape-dependent Reshape
relations have been derived. It then maps compatible Shape, Gather, Slice,
Squeeze/Unsqueeze, Concat, Split, OneHot, Identity, Cast, and
ConstantOfShape computations into the shared tensor algebra. Decompositions
copy readable result names and all non-frontend operation attributes; the
`onnx` operation descriptor is removed only after its values are materialized.
Unsupported ranks and malformed spatial attributes are left unchanged rather
than guessed. `NOTSET`, `VALID`, `SAME_UPPER`, and `SAME_LOWER` padding share
one explicit two-dimensional padding relation used by both convolution and
pooling. Open intermediate types are likewise retained instead of causing an
unsafe projection. Named symbolic extents now flow through Add/Sub/Mul,
Flatten, rank-two-or-higher MatMul, Gemm, and Transpose when equality,
singleton broadcasting, permutation, or a directly representable partition
product proves the result; ambiguous symbolic arithmetic remains at the ONNX
frontier.
Schema-only result facts remain usable under partial information: NonZero
preserves its rank-by-count matrix, NonMaxSuppression preserves its three-column
index result, and Range preserves a known scalar element type. Expand, Tile,
TopK, Resize-by-`sizes`, symbolic equal Split, and Squeeze contribute shapes
when their constant operands or selected axes prove them. Resize-by-runtime
`scales` and arithmetic over unrelated symbolic extents intentionally remain
open.
Each ONNX inference and conversion relation is an ordinary function carrying
open attributes owned by `onnx.nn`. The driver discovers those functions with
`ir.fns`, selects them with `ir.where`, and executes them with `ir.invoke`.
Conversion relations use `phase` only to preserve the module's explicit
compute-then-shape order. `onnx.nn.convert(m, rules)` executes an explicitly
provided relation set, selecting its inference and ordered conversion phases
internally. Another module can therefore append both a type relation and a
conversion relation without editing or copying this module; `convert(m)` uses
the built-in functions as the complete default set. Import remains a separate
codec operation.
Convolution and pooling preserve symbolic batch or channel terms while
requiring only the spatial extents used by their arithmetic to be integer
literals. Conv accepts its schema's optional one-dimensional bias and maps it
through the existing layout-explicit `nn.conv2d` composition. A mismatched bias,
channel relation, nonpositive stride/dilation, or malformed automatic padding
keeps the source call intact.
`onnx.nn.convert` first runs that deterministic source-order propagation, then
maps static and dynamic quantization, integer and scaled
transposed MatMul, Cast, Conv, BatchNormalization, ReLU, LeakyReLU, inference
Dropout, Add/Sub/Mul/Div/Pow, Gemm,
Sqrt/Reciprocal/Tanh, AveragePool, MaxPool, GlobalAveragePool, ReduceMean,
Softmax, Reshape, Flatten, rank-two-or-higher MatMul, and Transpose. Flatten
reuses `tensor.reshape`; MatMul reuses `tensor.matmul`; Transpose reuses
`tensor.permute`; Gemm reuses a general `nn.gemm` body with transpose flags,
alpha/beta scaling, and broadcast bias. The relation materializes schema
attributes as ordinary operands and removes schema-only shape inputs. `infer`
remains separately callable when a researcher wants to inspect or transform the
typed source graph,
but the common conversion path needs only one explicit function call and never
runs during import or module loading. `ir.retarget` accepts each prospective
call through the ordinary resolver before committing it, so partial or
anonymous shapes retain only the unsupported source call. On the pinned
MobileNetV2, SqueezeNet 1.1, QDQ SqueezeNet 1.0, ResNet-18, and Tiny-YOLOv2
suite this covers every compute node; unsupported calls in other models remain
untouched. Tiny-YOLOv3 pins its current incomplete type frontier so new
relations cannot silently regress complex control-flow graphs. UltraFace is a
full semantic gate: initializer and Constant tensor literals share one decoder,
and old attribute-form and current input-form Slice share one relation before
conversion to explicit operands.
The larger SSD-MobileNetV1 fixture is a partial semantic gate. Its eight nested
graphs and nearly six thousand calls validate transport, graph-interface type
flow, and conservative partial shapes. The pinned frontier falls from 6,790 to
4,682 unknown results; unsupported relations remain measurable source calls.
ShuffleNet V2 is a full inference/conversion gate. DenseNet-121 adds an
inference-from-signature gate that erases all 910 intermediate annotations and
requires the module to recover every one from inputs and constants.
Softmax conversion accepts an explicit, in-range ONNX axis and normalizes a
negative value before calling the shared body. An omitted axis stays in the
source namespace because its default depends on the imported opset.
After a successful mapping, source metadata is removed because its semantic
fields are now explicit operands and the readable result binding already
preserves node identity. Consequently the normal `ir.expand` operation can
expose any converted function body without a special metadata exception.

`ir.call` inserts an arbitrary call immediately before an existing operation.
A `str` result-type argument returns the single `Val` convenience form; a
`list<str>` returns the created `Op`, whose values are available through
`ir.outs`; an empty list creates a visible zero-result call. `ir.rename` is
likewise overloaded for a call target or a result name. The insertion point
makes order explicit and lets the core reject non-dominating operands without
a stateful builder object. For example, a
module can select functions carrying `[rewrite: "my.fused"]`, inspect their
calls, create `my.fused(...)`, redirect uses, and erase the old calls. The same
metadata mechanism can describe entry points, optimization stages, target
capabilities, cost hints, provenance, or test groups; their interpretation
belongs entirely to the module that queries them.

`ir.ops(m)` is the concise default traversal: it returns all operations in
function order and structural preorder, including nested loops and conditions.
`ir.ops(f)` restricts that walk to one function, while `ir.ops(b)` returns only
the immediate operations of one `Blk`. `ir.replace` replaces all uses by
default; its four-argument overload changes only uses in one named `Op`.
Both forms check type compatibility and dominance before changing the IR.

`ir.find(m, name)` performs exact local function lookup and returns an invalid
`Fn` when the symbol is absent; `ir.live` is the uniform validity test.
`ir.params(f)` and `ir.returns(f)` expose both sides of the same declared
signature. Together they let format modules reflect nested source graphs or
other function-valued metadata without a format-specific function handle.

Every valid `Blk` ends in `return` or internal `yield`, so an existing `Op` is
also a complete insertion position; no ambient builder or special append state
is needed. `ir.constant` and `ir.call` insert leaves. `ir.clone` recursively
copies a call, constant, loop, or condition, creates fresh `Blk`s/results, and
remaps values defined inside the copied subtree. `ir.move` reorders an operation
within its `Blk` atomically and rejects the change if any use would lose
dominance. `ir.kind` and `ir.blks(op)` make structural selection explicit.

`ir.loop` creates iterator and carried `Blk` arguments plus an initial
forwarding yield. `ir.branch` creates two initially forwarding arms. A module
populates either structure by inserting ordinary calls or constants before its
yield, then reconnects the terminator with `ir.args(m, op, values)`. The same
argument mutator updates an existing return. Named local carried values recover
as ordinary `var` bindings when printed, so the construction API does not leak
an auxiliary `Blk` syntax into `.jog`.

`ir.rename` may be applied directly to a `Blk` argument. Iterator renames are
reflected in the loop header, while carried-value renames propagate through
both arms, yields, and enclosing structured results. The operation therefore
preserves printable lexical bindings rather than changing only one internal
handle label.

### Optimization functions

The bundled `opt` module demonstrates composition without a pass hierarchy.
`opt.unresolved` returns the distinct unresolved call names in structural order,
so a frontend bridge or target can audit semantic coverage without a registry
or a built-in operator catalogue. The hidden `base.list` normalization used by
list literals is language structure and is not reported as an external call.
`fold_identity` applies an explicit binary identity, `cse` merges structurally
identical same-`Blk` calls, and `dce` removes unused calls. The latter two take
a list of callees the caller asserts are pure; no unknown computation is
silently treated as removable. `fix` composes these transforms for at most the
requested number of rounds, while `basic` supplies a small algebra-only entry
point. A research module can call the individual functions or wrap `fix` with
its own purity policy using normal `.jog` code.

`opt.expand(m, callees)` exposes one level of the named function bodies. A
snapshot traversal deliberately does not recurse into calls created by the
same invocation, so the caller controls abstraction: one step may expose
`nn.linear` as `tensor.matmul` plus a bias loop, and a later step may expose
`tensor.matmul` as explicit nested loops.

`opt.legalize(m, caps, limit)` is the capability-driven form. `caps` is a
`list<Fn>` owned by the consumer. A call is retained only when its resolved
semantic symbol matches a declaration's local name and `ir.accepts` proves its
argument and result types satisfy that declaration's generic signature. Every
other metadata-free call with a visible body is expanded, one layer per round.
The caller bounds recursion with `limit`. Calls with no visible body and calls
carrying operation metadata remain intact because guessing either an
implementation or a metadata distribution policy would change semantics.

`opt.frontier(m, caps)` returns the distinct remaining calls not covered by the
same capability list. It is a read-only query, so `len(opt.frontier(...)) == 0`
is a simple readiness test. A target experiment can describe its accepted
computation with ordinary functions:

```jog
module edge
use opt
use ir
use tensor

fn tensor.matmul<M: int, N: int, K: int>(
  a: tensor<i8, [M, K]>, b: tensor<i8, [K, N]>
) -> tensor<i8, [M, N]>;

fn caps() -> list<Fn> {
  return ir.fns("edge")
}

fn prepare(m: Mod) -> bool {
  return opt.legalize(m, caps(), 16)
}
```

There is no capability registry or target base class. `ir.fns("edge")`
enumerates the loaded module without importing it into the model; unrelated
helper functions cannot match a source symbol and are ignored. Renaming or
selecting retained calls remains another normal module function. `base.list`,
the language's internal materialization of list literals, is structural and is
ignored by capability checks.

A body-bearing declaration is also an alternative implementation. `opt.apply`
groups declarations by the resolved source symbol, asks `ir.match` to choose
the most specific compatible overload, and expands that body to a fixed point.
The default bound is derived from the number of supplied implementations;
`opt.apply(m, impls, limit)` makes it explicit for recursive specialization.
One extra convergence probe detects a still-changing final round; failure
diagnoses the bound and rolls the complete invocation back. Bodyless
declarations are ignored by `apply` and remain useful to `legalize`. If an
implementation module is not visible from the model, the environment adds one
`use` edge before expansion so unqualified helper calls in the copied body keep
their defining visibility. Dependency insertion and expansion are one
transaction; a mismatch or unrepresentable generic restores the original IR
and revision. Metadata-bearing calls remain explicit until the owning module
chooses how their tags should be distributed.

```jog
module edge
use tensor
use opt
use ir

fn dot<M: int, N: int, K: int>(
  a: tensor<i8, [M, K]>, b: tensor<i8, [K, N]>
) -> tensor<i8, [M, N]>;

fn tensor.matmul<M: int, N: int, K: int>(
  a: tensor<i8, [M, K]>, b: tensor<i8, [K, N]>
) -> tensor<i8, [M, N]> {
  return dot(a, b)
}

fn prepare(m: Mod) -> bool {
  return opt.apply(m, ir.fns("edge"))
}
```

More-specific overloads can describe a fixed vector width, tile shape, number
format, or fused implementation while a generic overload remains the fallback.
The resulting calls are still ordinary `Op`s in ordinary `Fn` bodies; `dot`
has no built-in target meaning.

`opt.rename(m, rules)` applies exact call-name pairs supplied as
`list<list<str>>`. It knows no frontend or network names. A bridge first calls
`ir.use` for its destination library, then supplies a relation such as
`[["onnx.Relu", "nn.relu"]]`; final verification checks that the renamed
call actually matches a visible destination function. Rules that need operand
reordering, attribute interpretation, or new constants remain ordinary bridge
code rather than hidden behavior in this simple relation helper.

The C++ embedding API can call the same function as
`run(env, "module.fn", mod)` or request a structural report with
`run(env, "module.fn", mod, report)`. Reports are `Attr` dictionaries, so tools
can serialize or extend them without linking to a report-class ABI. They expose
the returned change claim separately from the observed revision delta and
include nested transform completions. Entries use `kind: "fn"`; successful
`ir.expand` edits additionally contribute `kind: "expand"` entries containing
`source`, `impl`, `params`, `returns`, and the exact revision interval. This is
enough to audit overload selection without an implementation-plan object or a
second dry-run algorithm.
`joggle run module.fn model.jog --report run.attr -M modules` writes that same
structural report separately while preserving the transformed module on
standard output. The implementation reuses the public `print(Attr)` overload,
so the CLI does not own a second serialization schema.
`joggle query module.fn model.jog -M modules` invokes a no-extra-argument
analysis and writes its canonical `Attr` result. `opt.unresolved` reports calls
without a visible declaration; the complementary `opt.untyped` reports calls
whose outputs still have the open `_` type. The distinction separates symbol
coverage from type-propagation coverage.
`joggle emit module.fn model.jog -M modules` invokes the same read-only function
but requires `str` or `bytes` and writes the payload verbatim. A module can
therefore expose source, HDL, assembly, or a binary image without implementing
an emitter interface or changing the CLI for its artifact kind.

`c.source` demonstrates the complete path in pure `.jog`. It reflects local
functions, maps scalar types and fixed C operators from ordinary dictionaries,
prints local calls and structured control flow, flattens statically shaped
tensor indexing, and returns C99 text. Static tensor results become explicit
caller-owned output pointers. A non-local call is not implicitly lowered:
the module reports that it must be exposed first. Overloads or sanitized names
that would collide in C are rejected before text is returned. The execution
test emits a matrix multiplication plus scalar call/branch functions, compiles
them with a system C compiler under warnings-as-errors, and checks their
numerical results.

`c.prepare` is a separate, explicitly selected transform. It asks the same
module-owned structural predicate whether a call is directly printable and
expands unsupported calls only when their ordinary resolved function has a
body and carries no unhandled metadata. The bounded fixed point is
transactional and a second preparation is byte-identical. In the execution
gate, a high-level tensor addition expands
through the shared `tensor` body into a constructor, scalar-list shape loop,
range loop, indexing, and scalar addition; that prepared model is then emitted,
compiled, and executed. `c.source` does not invoke the transform.

A read-only module function is invoked with `query(env, "module.fn", mod,
result, args, cached)`. It is still declared with ordinary `fn` syntax. The
call mode evaluates a verified snapshot, rejects attempted edits, and caches
the single structural result using the module revision, environment load
epoch, function specialization, and explicit `Attr` arguments. For example,
`opt.count(m, callee)` counts live calls without introducing an analysis class
or metadata convention.

Embedding code that chooses steps dynamically may pass a
`span<const string_view>` to `run`. The overload executes the named functions
in order, returns their ordinary reports in a `steps` list, and treats the
whole sequence as one transaction. This is the host-side equivalent of writing
a normal `.jog` wrapper function; it does not register, own, or serialize a
pipeline object.

The bracket syntax is not a `host` special case. Any module may define its own
keys and attach them to a function, value binding, or operation statement.
Version and ABI information remains in package/API data; it is not encoded in
function names or required in module source.

`ir.fuse` is likewise operator-neutral. It accepts an ordered `list<Op>`,
derives unique live-ins and the single live-out, inserts the requested call,
and removes the region transactionally. It rejects mixed `Blk`s, reordered or
duplicate operations, multiple live-outs, invalid dominance, and fusion across
an unselected executable operation. `opt.fuse` is a normal `.jog` helper that
finds a single-use call chain from a user-supplied list of callee names; a
frontend bridge can invoke it explicitly without registering operator classes
or modifying the core.

### Binary codecs

`joggle read module.function input` is the common frontend boundary. It reads
the input as `bytes`, invokes a bound native function returning `str`, then
parses and verifies that string as an ordinary `Mod`. The optional ONNX module
implements `onnx.read` with generated Protobuf Lite code. Protobuf is linked only
into `joggle_onnx`; the core library and normal build remain dependency-free.

The current codec accepts dense ONNX tensors, tensor-shaped graph values,
scalar/list/tensor/graph node attributes, arbitrary node result counts, and
multiple graph outputs. Every graph-valued attribute becomes an ordinary local
`Fn`. Values read from an enclosing ONNX graph become explicit trailing
parameters, and the graph reference records their positions in the owning
call's operands. Recursive captures propagate through nested functions, so no
frontend-only region tree is required. Types from graph inputs, outputs,
intermediate `value_info`, and initializers become explicit result annotations
where available. Missing
optional node outputs retain their result position through an unused binding.
Each distinct ONNX `dim_param` becomes an `int` generic on the imported
function, so repeated symbolic dimensions retain identity across inputs,
intermediates, and outputs. Sanitized name collisions are resolved once and
value bindings cannot shadow those generics. An unnamed dynamic dimension is
the ordinary open term `_`.
Unsupported sparse, string, and external-data forms fail with a diagnostic
rather than being dropped. Operator names and attributes are
transported generically; their semantics belong to later modules. Data inputs
remain call operands. Node names and schema attributes live on the `Op`, while
each nonempty original value name lives on its `Val` under the same open
`onnx` key. This preserves source identity through identifier normalization
without changing a call's semantic arity.

The optional `tflite` codec is a second implementation of the same boundary.
It emits every subgraph as a function, tensors as typed values, buffer-backed
tensors as payload calls, and operators as open `tflite.*` calls. FlatBuffers
mini-reflection transports every schema-known option table into the operation's
`tflite` metadata dictionary without dispatching on operator names. Optional
input slots remain `nil`, and multiple outputs remain ordinary call results.
Every source tensor attaches its index and original name to the corresponding
`Val`; nonzero buffer identity and optional quantization, sparsity, and variable
state are included when present. Types do not repeat in metadata. Thus a bridge
or target can inspect data semantics independently of producer opcode.
Negative extents in `shape_signature` become `_`, which now satisfies the same
open integer-term rule as an anonymous ONNX dimension.
The checked-in schema is upstream source; its large generated C++ interface is
private build output. As with ONNX, mapping those source calls to `nn` is the
responsibility of a separately selected relationship module.

That relationship is the pure `.jog` module `tflite.nn`. Its `convert`
function materializes padding, stride, dilation, groups, logical axes, fused
activation, and softmax axis/scale as normal operands. Standard and depthwise Conv,
Add/Sub/Mul, average/max pool, reshape, and softmax then resolve to shared
functions. Each mapping is an ordinary function selected by its module-owned
`on` attribute through the same `ir.where`/`ir.invoke` boundary as ONNX; there
is no frontend-wide operator dispatch chain. Its two-argument `convert`
overload lets a caller supply a composed relation set while retaining this
module's dependency preparation and quantization guard. On
the pinned MobileNetV2 this removes all 66 source compute calls while retaining
the source model marker and payloads. A second invocation is unchanged, and
all 66 converted bodies can be independently exposed and round-tripped.
Each successful mapping uses the same atomic `ir.retarget` operation as the
ONNX bridge, so adding layout, padding, activation, or axis operands cannot
expose an intermediate call with the source callee and target arguments.

The same bridge maps unquantized Add, Sub, and Mul through the shared broadcast
semantics. Before any conversion it checks every operand and result for
nonempty scale and zero-point metadata. Quantized calls are deliberately left
intact: their rescaling, rounding, and saturation must be made explicit by a
quantization module rather than approximated by raw integer arithmetic.

### A hardware extension

`sat` is a complete, intentionally small module for signed saturating integers.
It demonstrates the five pieces a hardware experiment commonly needs without
turning them into five plugin kinds:

| Function | Role |
| --- | --- |
| `sat.add<W>` | Primitive over the module-defined `sat<W>` format. |
| `sat.supports` | Type predicate used by selection policy. |
| `sat.select` | Textual transform from matching `operator +` calls. |
| `sat.sim` | Bit-exact scalar reference semantics. |
| `sat.emit` | SystemVerilog text for the selected-width primitive. |

Build it with `JOGGLE_BUILD_SAT=ON`. The declaration, transformation policy,
reference semantics, and emitted representation stay together in the module;
the core knows none of their names. A research module can replace any or all of
these functions without adopting a target class hierarchy.

`module.jog` contains the module header and imports. Files in `lib/*.jog` are
appended in lexical path order and contain further declarations without another
module header. This gives one deterministic in-memory `Mod`, not one IR per
source file.
