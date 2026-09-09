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

The standard modules are deliberately narrow: `base` declares value copying,
`tensor` declares the open `tensor<E, S>` type constructor, `ir` declares
universal reflection functions, and `opt` contains a real textual transform.
The optional `onnx` module adds binary model import without a core operator
switch. Future NN
semantics, MLIR, JIT, simulation, hardware description, and target experiments
remain removable modules.

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
closure. A local function family shadows imported families; otherwise matching
imported declarations form one deterministic overload set. Merely loading
another module into the same `Env` does not make its declarations visible, and
missing `use` edges are diagnosed. Resolution checks arity and structural
types, infers generic arguments, ranks specificity, and computes result types.
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
| `fns`, `blocks`, `ops` | Traverse structural ownership in stable order. |
| `args`, `outs`, `users` | Read operation dataflow. |
| `callee`, `type`, `is_const`, `constant`, `len` | Query calls, values, and lists. |
| `has`, `meta` | Query open function or operation attributes. |
| `call`, `replace`, `erase`, `rename` | Build and rewrite calls through the same checked mutations as C++. |
| `set`, `unset` | Add, replace, or remove a function or operation attribute. |

These functions operate on generic handles and contain no NN operator names.
Adding an importer, optimization, or target module therefore does not extend
the reflection ABI or add a parser case.

`ir.call` inserts an arbitrary call immediately before an existing operation.
A `str` result-type argument returns the single `Val` convenience form; a
`list<str>` returns the created `Op`, whose values are available through
`ir.outs`. `ir.rename` is likewise overloaded for a call target or a result
name. The insertion point makes order explicit and lets the core reject
non-dominating operands without a stateful builder object. For example, a
module can select functions carrying `[rewrite: "my.fused"]`, inspect their
calls, create `my.fused(...)`, redirect uses, and erase the old calls. The same
metadata mechanism can describe entry points, optimization stages, target
capabilities, cost hints, provenance, or test groups; their interpretation
belongs entirely to the module that queries them.

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
scalar/list/tensor node attributes, single-output nodes, and one graph output.
Unsupported sparse, string, external-data, nested-graph, or multi-output forms
fail with a diagnostic rather than being dropped. Operator names and attributes
are transported generically; their semantics belong to later modules.

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
