# Extensions

An extension consists of canonical Module source and, only when necessary, a
platform-specific behavior library. Source owns declarations, signatures,
interfaces, defaults, and dependent types. C++ supplies behavior that cannot
or should not be written as a Joggle body.

## Organize by vocabulary

A Module should own one coherent vocabulary, not one position in a global
pipeline. Typical boundaries are a model format, tensor algebra, numeric
format, target instruction family, layout system, simulator state, or emitted
artifact.

Conversions belong to bridge Modules:

```joggle
module tensor_to_accel@1.0.0 {
  import tensor@2.0.0;
  import accel@1.0.0;

  fn convert(input: module, target: accel.target) -> module;
}
```

This avoids baking a frontend/backend direction into either vocabulary. A
different bridge may convert in the opposite direction or combine both with an
analysis result.

## Compiler functions are ordinary functions

The language has no separate pass declaration. A function becomes compiler
work because its inputs and results have compiler representations:

```joggle
fn load(path: string) -> module;
fn canonicalize(input: module) -> module;
fn cost(input: module, target: target) -> estimate;
fn emit(input: module) -> bytes;

fn compile(path: string, target: target) -> bytes {
  module = canonicalize(load(path));
  return emit(module);
}
```

The same call rules compose module operations and compiler functions. A body
can branch on Known configuration and use `for` over Known lists. Calls that
depend on Residual module values remain in the executable IR.

Prelude declares `module`. The compiler automatically represents it as
`joggle::Module`, which carries multiple named Functions through a
pipeline. No repository import or manual representation registration is
required.
This is a whole-module value, not a second IR hierarchy.

## Bind a function

After linking, bind a local function name to a matching C++ callable:

```cpp
const auto module = compiler.module("metrics");
if (module) {
  compiler.bind(*module, "volume",
                [](const std::vector<std::int64_t>& shape) {
    std::int64_t result = 1;
    for (std::int64_t dimension : shape) {
      result *= dimension;
    }
    return result;
  });
}
```

The callable signature selects an overload and is checked immediately. No
wrapper, generated declaration header, or per-function adapter is needed.
`Module::FunctionDecl` can still be reflected and passed directly when an
implementation also needs the declaration as an IR rewrite target.

Supported compiler-domain mappings are `std::int64_t`, `double`, `bool`,
`std::string`, `joggle::Type`, `joggle::Attribute`, `joggle::Bytes`, and
homogeneous `std::vector<T>` forms. Whole-IR functions use
`joggle::ir::Function` or `joggle::Module`.

A no-result declaration binds to C++ `void`; one result binds to `T`; multiple
results bind positionally to `std::tuple<Ts...>`. Returning
`std::optional<T>` or `std::optional<std::tuple<Ts...>>` reports ordinary
execution failure when empty. No result-wrapper class is generated.

A binding may optionally receive `joggle::Compiler&` first and
`joggle::Diagnostics&` last. `ir::Function` and `Module` are copy-on-write
values: a transformation accepts and returns them by value, while a read-only
analysis may accept `const&`. Ordinary `fn` inputs cannot bind to mutable
lvalue references because that would introduce an undeclared in-place result.
The C++ result therefore matches the declared result instead of using a hidden
success convention. Signature mismatches are reported when binding, not
deferred to invocation.

Before a binding receives a `Function` or `Module`, the compiler validates its
materialized IR against the linked contracts and extension verifiers. Returned
artifacts are checked again before they can flow to the next typed function.

Bindings default to guarded host evaluation. Use the explicit
`HostEvaluation` policy only when the implementation's determinism and effects
are understood; non-Hermetic work is never speculated under Residual control.

## Register a host representation

Module-declared compiler types can use project-native C++ values without a
generated wrapper:

```cpp
struct Target {
  std::int64_t lanes;
  std::string architecture;
};

compiler.represent<Target>(
    *target_decl,
    [](const Target& target) {
      return std::tuple{target.lanes, target.architecture};
    });
```

The projection order is the declaration's parameter order. The Module remains
the type authority; registration only supplies storage and projection for host
invocation.

The core registers the Prelude `module` representation before linking.
Extension-defined artifacts, cost estimates, schedules, and device descriptions
use the public registration mechanism and require no core class.

## Verifiers and interface behavior

`compiler.verify(declaration, callback)` attaches semantic checks to a Type,
Attribute, or residual Instruction declaration. The callback receives
`const joggle::Type&`, `const joggle::Attribute&`, or
`const joggle::ir::Instruction&`; it returns `bool` and may accept Diagnostics
last. This explicit API keeps `bind` reserved for implementations whose C++
inputs and outputs match a declared `fn`. There is no verifier declaration kind
or trait class. Core verification always checks ownership, arity, types, CFG
structure, SSA, and declaration contracts before extension verifiers run.

Interface methods are bound against a reflected method declaration and called
on an Attribute or Instruction. Type-interface fields are different: they are
declarative values computed by the type declaration and read with
`Type::get<T>`; they are not dynamic callbacks.

## Behavior libraries

A behavior library exports one versioned descriptor:

```cpp
void bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  // Reflect declarations and attach behavior.
  return true;
}

JOGGLE_EXPORT_BEHAVIOR(bind)
```

Build it with:

```cmake
find_package(Joggle CONFIG REQUIRED)
joggle_add_behavior(my_behavior
  MODULE my_module.joggle
  SOURCES behavior.cpp
)
```

The descriptor records ABI, host target, and exact canonical Module identity.
Loading rejects the wrong ABI, target, or Module digest before callbacks run.
Binding is transactional: reporting any diagnostic rolls back every callback
installed by the library, so there is no second success-result convention.
Behavior libraries contain implementations only; they cannot introduce hidden
declarations.

## Transformation discipline

Edit Functions through `Function::Edit`. The edit is transactional: commit
runs structural and semantic verification; a failed or abandoned edit restores
the previous Function.

Prefer declarations for concepts that must survive serialization. Prefer C++
only for algorithms, external libraries, file formats, or host integration.
Do not encode target policy in the core merely to make one extension easier;
declare a target type and pass it explicitly through typed compiler functions.
