# Build an extension

A Joggle extension starts with one text Module. C++ is needed only for domain
checks, runtime interface behavior, or a compiler function that cannot be
expressed by contraction rules. Compile-time functions and derived type parameters stay in
the Module and need no C++.

## 1. Write the Module

```joggle
joggle 1;

module external@1.0.0 {
  type scalar(bits: int);

  fn keep<T: type>(input: T) -> T;
  fn converted<T: type>(input: T) -> T;

  fn convert(input: graph) -> graph;

  fn main(input: scalar<8>) -> scalar<8> {
    output = keep(input);
    return output;
  }
}
```

Run `joggle check external.joggle` before writing C++. Types, function
contracts, call references, local dataflow, and result inference are checked from
this file alone. During multi-Module development, add uninstalled dependencies
with `--with dependency.joggle`; installation is not required for validation.

## 2. Attach optional behavior

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

  compiler.bind(*convert, [keep = *keep, converted = *converted](
      joggle::Graph& graph, joggle::Diagnostics& pass_diagnostics) {
    const auto operations = graph.all_operations();
    auto edit = graph.edit();
    for (const auto& operation : operations) {
      if (operation.schema() == keep) {
        edit.replace(operation, converted);
      }
    }
    return edit.commit(pass_diagnostics);
  });
  return true;
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
```

The compiler function resolves the declarations it compares once, then edits the same
`Graph` opened from the text member. Type and interface behavior use the same
binding form when an extension needs them; the complete cases live in
[C++ behavior bindings](bindings.md).

## 3. Build it

```cmake
cmake_minimum_required(VERSION 3.20)
project(External LANGUAGES CXX)

find_package(Joggle CONFIG REQUIRED)

joggle_add_behavior(external_behavior
  MODULE external.joggle
  SOURCES behavior.cpp
)
```

`joggle_add_behavior` ties the library to the exact Module source. Handwritten
C++ includes only Joggle's generic API.

## 4. Check and install it

```bash
joggle check external.joggle --behavior build/libexternal_behavior.so
joggle install external.joggle \
  --behavior build/libexternal_behavior.so
```

Installation atomically publishes canonical Module text and the target-specific
behavior binary under their SHA-256 identities. The exact filename suffix is
platform-specific.

Before installation, the same source and behavior can run as a scriptable
compiler pipeline:

```bash
joggle run external.joggle main convert \
  --behavior build/libexternal_behavior.so \
  -o converted.joggle
```

The first positional member is the dataflow function and each following member
is a compiler function,
executed in order. Local names are relative to the root Module; qualified names
select a function from another explicitly loaded Module. `--with target.joggle`
adds such a Module without turning it into a source import, and
`--load-behavior target=build/libtarget_behavior.so` attaches its optional
implementation. The output is a complete canonical derived Module: it imports
the exact linked versions and contains the transformed graph. Here its Module
name is `external_main_compiled`, so it can coexist with `external` and with
artifacts from other graph members. It can be checked, installed, or used as the
input to another `joggle run`; `-o` publishes it atomically and replay preserves
the derived name.

## 5. Load and use it

```cpp
joggle::Compiler compiler;
compiler.search(module_root);
compiler.load(installed_module);
if (!compiler.link()) {
  compiler.diagnostics().print(std::cerr);
  return 1;
}

auto module = compiler.module("external");
compiler.load_behavior("external");
auto graph = compiler.graph("external.main");
compiler.run(*graph, "external.convert");

auto inputs = graph->inputs();
auto outputs = graph->outputs();
```

The executable fixture in
[`tests/consumer`](../tests/consumer) performs this complete workflow against a
fresh installed Joggle package on every test run. It is the authoritative
copyable project; this guide explains the same files rather than defining a
second example architecture.
