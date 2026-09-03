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
  import tensor@1.0.0;
  import accel@1.0.0;
  import ir@1.0.0;

  fn convert(input: ir.module, target: accel.target) -> ir.module;
}
```

This avoids baking a frontend/backend direction into either vocabulary. A
different bridge may convert in the opposite direction or combine both with an
analysis result.

## Compiler functions are ordinary functions

The language has no separate pass declaration. A function becomes compiler
work because its inputs and results have compiler representations:

```joggle
fn load(path: string) -> ir.module;
fn canonicalize(input: ir.module) -> ir.module;
fn cost(input: ir.module, target: target) -> estimate;
fn emit(input: ir.module) -> bytes;

fn compile(path: string, target: target) -> bytes {
  program = canonicalize(load(path));
  return emit(program);
}
```

The same call rules compose program operations and compiler functions. A body
can branch on Known configuration and use `for` over Known lists. Calls that
depend on Residual program values remain in the executable IR.

`modules/ir.joggle` declares `ir.module`. Its standard C++ representation is
`joggle::ir::Module`, which can carry multiple named Functions through a
pipeline. This is a transport type, not a second IR hierarchy.

## Bind a function

After linking, look up the declaration and bind a matching C++ callable:

```cpp
const auto module = compiler.module("metrics");
const auto volume = module ? module->function("volume") : std::nullopt;
if (volume) {
  compiler.bind(*volume, [](const std::vector<std::int64_t>& shape) {
    std::int64_t result = 1;
    for (std::int64_t dimension : shape) {
      result *= dimension;
    }
    return result;
  });
}
```

Supported compiler-domain mappings are `std::int64_t`, `double`, `bool`,
`std::string`, `joggle::Type`, `joggle::Attribute`, `joggle::Bytes`, and
homogeneous `std::vector<T>` forms. Whole-IR functions use
`joggle::ir::Function` or `joggle::ir::Module`.

A binding may optionally receive `joggle::Compiler&` first and
`joggle::Diagnostics&` last. A `Function&` input must remain a reference so a
transform does not accidentally consume the artifact. Signature mismatches are
reported when binding, not deferred to invocation.

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

The standard `ir.module` representation is registered through this same
mechanism. Extension-defined artifacts, cost estimates, schedules, and device
descriptions require no core class.

## Verifiers and interface behavior

Binding a callable whose first argument is `const joggle::Type&`,
`const joggle::Attribute&`, or `const joggle::ir::Instruction&` attaches a
declaration verifier. It may return `bool` and optionally accept Diagnostics.
Core verification always checks ownership, arity, types, CFG structure, SSA,
and declaration contracts before extension verifiers run.

Interface methods are bound against a reflected method declaration and called
on an Attribute or Instruction. Type-interface fields are different: they are
declarative values computed by the type declaration and read with
`Type::get<T>`; they are not dynamic callbacks.

## Behavior libraries

A behavior library exports one versioned descriptor:

```cpp
bool bind(joggle::Compiler& compiler, const joggle::Module& module,
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
