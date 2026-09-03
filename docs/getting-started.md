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

## 2. Declare an extension

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

bool bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto keep = module.function("keep");
  const auto replacement = module.function("replacement");
  const auto rewrite = module.function("rewrite");
  if (!keep || !replacement || !rewrite) {
    diagnostics.report("example behavior does not match its Module");
    return false;
  }

  compiler.bind(
      *rewrite,
      [keep = *keep, replacement = *replacement](
          joggle::ir::Function& function,
          joggle::Diagnostics& edit_diagnostics) {
        const auto instructions = function.instructions();
        auto edit = function.edit();
        for (const auto& instruction : instructions) {
          if (instruction.callee() == keep) {
            edit.replace(instruction, replacement);
          }
        }
        return edit.commit(edit_diagnostics);
      });
  return true;
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
```

The callback is an ordinary binding for the declared `rewrite` function. It
edits `joggle::ir::Function` transactionally; no pass base class, generated
wrapper, Graph object, or Region API is involved.

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
hidden translation unit; extension code includes only the stable generic API.

## 5. Run a compiler function

```bash
joggle run example.joggle main rewrite \
  --behavior build/example_behavior.dylib \
  -o transformed.joggle
```

`main` is instantiated as executable IR, then each following function is
applied in order. The output is canonical Joggle source.

The same operation in C++ is direct:

```cpp
joggle::Compiler compiler;
compiler.load("example.joggle");
if (!compiler.link() ||
    !compiler.load_behavior("example", "build/example_behavior.dylib")) {
  compiler.diagnostics().print(std::cerr);
  return 1;
}

auto function = compiler.function("example.main");
if (!function || !compiler.run(*function, "example.rewrite")) {
  compiler.diagnostics().print(std::cerr);
  return 1;
}
```

For installed discovery, call `compiler.search(root)` and the one-argument
`load_behavior("example")`. See [Packages](packages.md) for repository and
lock semantics, and [`tests/consumer`](../tests/consumer) for the tested
installed-project example.
