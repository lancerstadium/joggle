# Build an extension

A Joggle extension is a text Module plus optional C++ behavior. The text file
is the schema authority; no declaration header is generated.

## 1. Declare a Module

```joggle
joggle 1;

module external@1.0.0 {
  type scalar(bits: int);

  fn make<N: int>(bits: N) -> scalar<N>;
  fn keep<T: type>(input: T) -> T;
  fn converted<T: type>(input: T) -> T;
  fn convert(input: function) -> function;

  fn main(input: scalar<8>) -> scalar<8> {
    output = keep(input);
    return output;
  }
}
```

`fn` is the only callable declaration. `main` describes residual program IR;
`convert` is used as compiler work because it accepts a Function value. This
does not create separate operation and pass namespaces.

Validate it before writing C++:

```bash
joggle check external.joggle
```

During multi-Module development, repeat `--with dependency.joggle` for local
dependencies that have not been installed.

## 2. Attach optional C++ behavior

```cpp
#include <joggle/joggle.h>

namespace {

bool bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto keep = module.function("keep");
  const auto converted = module.function("converted");
  const auto convert = module.function("convert");
  if (!keep || !converted || !convert) {
    diagnostics.report("behavior does not match external.joggle");
    return false;
  }

  compiler.bind(*convert,
    [keep = *keep, converted = *converted](
        joggle::Function& function,
        joggle::Diagnostics& pass_diagnostics) {
      const auto instructions = function.instructions();
      auto edit = function.edit();
      for (const auto& instruction : instructions) {
        if (instruction.callee() == keep) {
          edit.replace(instruction, converted);
        }
      }
      return edit.commit(pass_diagnostics);
    });
  return true;
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
```

The callback edits the ordinary `Function`. There is no `Graph`, `Region`,
generated declaration class, or pass subclass.

## 3. Build the behavior

```cmake
cmake_minimum_required(VERSION 3.20)
project(External LANGUAGES CXX)

find_package(Joggle CONFIG REQUIRED)

joggle_add_behavior(external_behavior
  MODULE external.joggle
  SOURCES behavior.cpp
)
```

`joggle_add_behavior` embeds the exact canonical Module identity in one hidden
translation unit. Handwritten code includes only the generic Joggle API.

## 4. Check and install

```bash
joggle check external.joggle --behavior build/external_behavior.dylib
joggle install external.joggle \
  --behavior build/external_behavior.dylib
```

The platform-specific suffix may be `.so`, `.dylib`, or `.dll`. Installation
publishes canonical Module text and optional behavior by content identity.

For a local transformation:

```bash
joggle run external.joggle main convert \
  --behavior build/external_behavior.dylib \
  -o transformed.joggle
```

`main` is instantiated as a Function. Each following name is an ordinary
compiler function applied in order. The output is another canonical Module
containing the resulting Function and exact imports.

## 5. Consume it from C++

```cpp
joggle::Compiler compiler;
compiler.search(module_root);
compiler.load(module_path);
if (!compiler.link() || !compiler.load_behavior("external")) {
  compiler.diagnostics().print(std::cerr);
  return 1;
}

auto function = compiler.function("external.main");
if (!function || !compiler.run(*function, "external.convert")) {
  compiler.diagnostics().print(std::cerr);
  return 1;
}

for (const auto& block : function->blocks()) {
  for (const auto& instruction : block.instructions()) {
    // Inspect the transformed Function.
  }
}
```

The executable project in [`tests/consumer`](../tests/consumer) is the
authoritative copyable example and is rebuilt against an installed Joggle in
the end-to-end test suite.
