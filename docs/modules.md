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
textual transforms, `tensor` defines storage-neutral tensor computation, and
`nn` contains network semantics. The optional `onnx` module only transports a
binary model. MLIR, JIT, simulation, hardware description, and target
experiments remain removable modules.

Version 0.1 searches explicit local paths. Installation means placing or
linking a directory on one of those paths; removal means taking it off the path.
Native libraries remain loaded for the lifetime of their `Env`. Network package
resolution, lockfiles, and in-process hot unloading are out of scope.

The bundled declarations install under `share/joggle/modules`. Applications
choose their module roots explicitly with `Env::path`; the core does not depend
on a process-global environment variable or a compile-time installation path.

Loading a module parses and verifies its declarations after loading dependencies.
It never runs a transform as a side effect. The caller selects an ordinary
function with `joggle::run` or `joggle run`; this keeps module installation,
function definition, and execution as three separate operations.

Qualified and imported calls resolve through the explicit and transitive `use`
closure. Local and imported declarations form one deterministic visible
overload set, allowing a specialized overload to reuse less-specific imported
algebra in its own body. Merely loading another module into the same `Env` does
not make its declarations visible, and missing `use` edges are diagnosed.
Resolution checks arity and structural types, infers generic arguments, ranks
specificity, and computes result types.
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
| `fns`, `params`, `blks`, `ops`, `uses` | Traverse function, structure, and dependencies. |
| `args`, `outs`, `users` | Read operation dataflow. |
| `live`, `block`, `kind`, `callee`, `type` | Query handle state and structure. |
| `resolve` | Resolve a call to its visible function declaration. |
| `is_const`, `constant` | Query constant IR values. |
| `has`, `meta` | Query open function or operation attributes. |
| `call`, `constant`, `loop`, `branch` | Construct leaves and structured control flow. |
| `clone`, `expand`, `move`, `args` | Copy, substitute a function body, place, or reconnect IR. |
| `replace`, `erase`, `rename` | Rewrite dataflow, ownership, and readable names. |
| `set`, `unset` | Add, replace, or remove a function or operation attribute. |
| `use` | Add an idempotent module dependency. |

These functions operate on generic handles and contain no NN operator names.
Adding an importer, optimization, or target module therefore does not extend
the reflection ABI or add a parser case.

General compile-time values live in `base`, not `ir`. `len` covers lists and
dictionaries; `keys`, `has`, and `get` expose deterministic dictionary access;
and `attrs["key"]` is the strict indexing form. These are enough for an
explicit bridge function to interpret frontend attributes without adding an
ONNX/TFLite field API or string-key cases to core. A missing strict key is a
diagnostic, while the three-argument `get` supplies a caller-chosen fallback.

### Tensor and network semantics

`tensor` defines `tensor<E, S>`, linear and two-dimensional indexing, `numel`,
elementwise addition, and matrix multiplication. Addition and matrix
multiplication have normal `.jog` bodies with loops and explicit value updates;
they are not opaque operator records. `nn.linear` composes matrix
multiplication with an optional bias loop, while `nn.relu` is a loop and
condition over the same tensor primitives. Transforms can therefore keep a
network call abstract or expose one function body at a time using the same
`Fn/Blk/Op/Val` representation.

`ir.resolve(m, op)` returns the declaration selected by the same structural
overload rules used by verification. `ir.expand(m, op, fn)` then substitutes
that normal function body, including nested loops and conditions. Generic
type, shape, and integer bindings are specialized at the call site; visible
result names and structured carried bindings remain printable. The C++ pair
`env.resolve(mod, op)` and `mod.expand(op, fn)` performs the identical edit.
`opt.expand` is only a policy helper over an explicit list of callees, not a
built-in lowering stage.

These definitions specify computation but deliberately do not choose layout,
memory space, vector width, tiling, device, or instruction. Such choices belong
to separately loaded research modules and can use open attributes or explicit
function arguments. The ONNX codec does not import `nn`; conversion between a
frontend schema and these functions must remain an explicit user-selected
module function.

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
the immediate operations of one block. `ir.replace` replaces all uses by
default; its four-argument overload changes only uses in one named `Op`.
Both forms check type compatibility and dominance before changing the IR.

Every valid block ends in `return` or internal `yield`, so an existing `Op` is
also a complete insertion position; no ambient builder or special append state
is needed. `ir.constant` and `ir.call` insert leaves. `ir.clone` recursively
copies a call, constant, loop, or condition, creates fresh blocks/results, and
remaps values defined inside the copied subtree. `ir.move` reorders an operation
within its block atomically and rejects the change if any use would lose
dominance. `ir.kind` and `ir.blks(op)` make structural selection explicit.

`ir.loop` creates iterator and carried block arguments plus an initial
forwarding yield. `ir.branch` creates two initially forwarding arms. A module
populates either structure by inserting ordinary calls or constants before its
yield, then reconnects the terminator with `ir.args(m, op, values)`. The same
argument mutator updates an existing return. Named local carried values recover
as ordinary `var` bindings when printed, so the construction API does not leak
an auxiliary block syntax into `.jog`.

`ir.rename` may be applied directly to a block argument. Iterator renames are
reflected in the loop header, while carried-value renames propagate through
both arms, yields, and enclosing structured results. The operation therefore
preserves printable lexical bindings rather than changing only one internal
handle label.

### Optimization functions

The bundled `opt` module demonstrates composition without a pass hierarchy.
`fold_identity` applies an explicit binary identity, `cse` merges structurally
identical same-block calls, and `dce` removes unused calls. The latter two take
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
include nested transform completions.

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
keys and attach them to a function or operation statement. Version and ABI
information remains in package/API data; it is not encoded in function names
or required in module source.

`ir.fuse` is likewise operator-neutral. It accepts an ordered `list<Op>`,
derives unique live-ins and the single live-out, inserts the requested call,
and removes the region transactionally. It rejects mixed blocks, reordered or
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
scalar/list/tensor node attributes, arbitrary node result counts, and multiple
graph outputs. Types from graph inputs, outputs, intermediate `value_info`, and
initializers become explicit result annotations where available. Missing
optional node outputs retain their result position through an unused binding.
Unsupported sparse, string, external-data, and nested-graph forms fail with a
diagnostic rather than being dropped. Operator names and attributes are
transported generically; their semantics belong to later modules.

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
