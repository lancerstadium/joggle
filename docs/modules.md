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

Pure `.jog` modules need no compiler toolchain. Native modules have one explicit
versioned C entry point and attach callbacks only to functions declared with
`[host]` in `.jog`. C++ STL containers, exceptions, RTTI, and virtual tables do
not cross that boundary.

```jog
module sample

[host]
fn ping(x: i32) -> i32;
```

```cpp
JOGGLE_MODULE_EXPORT bool joggle_module_v1(const jog_api_v1* api,
                                           jog_module_v1* module) {
  return api->abi_version == joggle::module_abi_version &&
         api->bind(module, "sample.ping", ping, nullptr);
}
```

The callback receives one versioned call frame for arguments, returns, and
diagnostics. Loading rejects bindings outside the declaring module, bindings to
unknown or non-host functions, duplicate bindings, missing entry points, and
ABI mismatches reported by the module. Calls validate scalar arguments and
returns against the `.jog` declaration. Scalars include length-delimited `str`
and `bytes`; embedded zero bytes are preserved.

The current standard modules are deliberately narrow: `base` declares value
copying, `tensor` establishes the tensor dependency boundary, `ir` declares
universal reflection functions, and `opt` contains a real textual transform.
Future `nn` and `onnx` modules may add model semantics and import without core
operator switches. MLIR, JIT, simulation, hardware description, and target
experiments remain optional.

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

The built-in `ir` module is the complete reflection boundary:

| Function | Meaning |
| --- | --- |
| `fns`, `blocks`, `ops` | Traverse structural ownership in stable order. |
| `args`, `outs` | Read operation dataflow. |
| `callee`, `is_const`, `constant`, `len` | Query calls, values, and lists. |
| `replace`, `erase`, `rename` | Apply the same checked mutations as C++. |

These functions operate on generic handles and contain no NN operator names.
Adding an importer, optimization, or target module therefore does not extend
the reflection ABI or add a parser case.

`module.jog` contains the module header and imports. Files in `lib/*.jog` are
appended in lexical path order and contain further declarations without another
module header. This gives one deterministic in-memory `Mod`, not one IR per
source file.
