# Getting started

This guide creates one installable declaration Module and one optional C++
behavior library. Joggle does not generate declaration headers: the `.joggle`
file remains the schema authority.

## 1. Build Joggle

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

For a separate consumer project, install Joggle and use
`find_package(Joggle CONFIG REQUIRED)`.

## 2. Declare a Module

Create `example.joggle`:

```joggle
joggle 1;

module example@1.0.0 {
  type word(width: int);

  fn keep<T: type>(input: T) -> T;
  fn replacement<T: type>(input: T) -> T;
  fn rewrite(input: function) -> function;

  fn main(input: word<8>) -> word<8> {
    return keep(input);
  }
}
```

Validate and format the source:

```bash
joggle check example.joggle
joggle fmt example.joggle --write
```

Use repeated `--with dependency.joggle` options for local imports that are not
installed yet.

## 3. Bind behavior

Create `behavior.cpp`:

```cpp
#include <joggle/joggle.h>

namespace {

void bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto keep = module.function("keep");
  const auto replacement = module.function("replacement");
  if (!keep || !replacement) {
    diagnostics.report("example behavior does not match its Module");
    return;
  }

  compiler.bind(
      module, "rewrite",
      [keep = *keep, replacement = *replacement](
          joggle::Function function,
          joggle::Diagnostics& edit_diagnostics)
          -> std::optional<joggle::Function> {
        const auto ops = function.ops();
        auto edit = function.edit();
        for (const auto& op : ops) {
          if (op.callee() == keep) {
            edit.replace(op, replacement);
          }
        }
        if (!edit.commit(edit_diagnostics)) {
          return std::nullopt;
        }
        return function;
      });
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
```

The callable's C++ input and output select and check the declared
`function -> function` overload. Only `keep` and `replacement` need explicit
declaration handles because the rewrite compares against their exact IR
identity. The input is an isolated value, and the returned Function is
published only after its edit commits; no pass base class, generated wrapper,
Graph object, or Region API is involved.

## 4. Build and validate behavior

```cmake
cmake_minimum_required(VERSION 3.20)
project(ExampleBehavior LANGUAGES CXX)

find_package(Joggle CONFIG REQUIRED)

joggle_add_behavior(example_behavior
  MODULE example.joggle
  SOURCES behavior.cpp
)
```

Then build and validate the exact source/binary pair:

```bash
cmake -S . -B build
cmake --build build
joggle check example.joggle --behavior build/example_behavior.dylib
```

Use the platform suffix produced by CMake: `.so`, `.dylib`, or `.dll`.
`joggle_add_behavior` embeds the canonical Module identity in a generated,
hidden translation unit; Module code includes only the stable generic API.

## 5. Run a compiler function

The in-process form directly materializes and transforms the example's
Function:

```cpp
joggle::Compiler compiler;
compiler.load("example.joggle");
if (!compiler.link() ||
    !compiler.load_behavior("example", "build/example_behavior.dylib")) {
  compiler.diagnostics().print(std::cerr);
  return 1;
}

auto function = compiler.materialize("example.main");
auto rewritten = function ? compiler.run<joggle::Function>(
                                "example.rewrite", *function)
                          : std::nullopt;
if (!rewritten) {
  compiler.diagnostics().print(std::cerr);
  return 1;
}
function = std::move(rewritten);
```

Command-line pipelines expose one portable file boundary:

```joggle
fn compile(input: bytes) -> bytes {
  return emit(optimize(read(input)));
}
```

```bash
joggle run driver.joggle compile model.onnx \
  --behavior build/driver_behavior.dylib -o model.bin
```

This wrapper uses `bytes -> bytes`. The CLI also accepts `bytes -> module`,
`module -> module`, and `module -> bytes`, so `read`, `optimize`, and `emit`
can be run individually without acquiring special declaration kinds. Module
files are linked and materialized at input and written as canonical source at
output. Byte results are written byte-for-byte to `-o`, or to standard output
when `-o` is absent.

For installed discovery, call `compiler.search(root)` and the one-argument
`load_behavior("example")`. See the
[Module repository](module-repository.md) for repository and lock semantics,
and [`tests/consumer`](../tests/consumer) for the tested installed-project
example.
