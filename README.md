# Joggle

Joggle is a lightweight C++ compiler framework for AI hardware/software
co-design. There is one program abstraction: `Graph`. A `Module` gives graphs
and their reusable declarations a versioned namespace and package; `Compiler`
loads Modules and runs passes on Graphs.

A `.joggle` file defines one Module. A model author normally imports an
operation vocabulary and writes graphs. An extension author may also declare
types, operations, and passes in the same file. References in graphs and passes
are the dependency information; they resolve through the Module's imports.
Generic C++ templates provide typed construction, named property access, and
optional behavior over the declarations in that source.

Build and test with standard CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /desired/prefix
```

Shared libraries are the default. `-DBUILD_SHARED_LIBS=OFF` produces the static
core and CLI. The test suite installs and consumes both variants from an
independent `find_package(Joggle)` project, including an external behavior
library.

```joggle
joggle 1;

module model@1.0.0 {
  import arith@1;

  graph example(%lhs: arith.integer<8>, %rhs: arith.integer<8>) -> arith.integer<8> {
    %sum = arith.add(%lhs, %rhs);
    return %sum;
  }
}
```

The imported `arith` Module declares `integer` and `add` once. The graph uses
those names directly. Authors defining a new hardware or numeric extension use
the same Module surface to add declarations and passes.

```cpp
joggle::Compiler compiler;
compiler.load("arith.joggle");
compiler.load("model.joggle");

if (!compiler.link()) {
  compiler.diagnostics().print(std::cerr);
  return 1;
}

auto arith = compiler.module("arith");
auto integer = arith->type("integer");
auto i8 = compiler.make(*integer, 8);
auto width = i8->get<std::int64_t>("width");

auto graph = compiler.graph("model.example");
```

```bash
joggle fmt module.joggle --write
joggle check examples/mlp.joggle \
  --with examples/arith.joggle --with examples/fixed.joggle \
  --with examples/tensor.joggle --with examples/nn.joggle
joggle run module.joggle main optimize -o optimized.joggle
joggle install examples/arith.joggle
joggle install examples/fixed.joggle
joggle install examples/tensor.joggle
joggle install examples/nn.joggle
joggle install examples/mlp.joggle
joggle install examples/edgevec.joggle
joggle install module.joggle --behavior build/behavior.dylib
joggle uninstall arith@1.0.0
joggle lock examples/mlp.joggle -o joggle.lock
```

The shipped examples form one vertical extension rather than unrelated demos.
`arith` defines scalar formats and basic arithmetic, `tensor` defines ranked
tensors, dense constants, and shape transforms, and `nn` defines model-level
operators. `fixed` independently implements the scalar-format interface. The
`mlp` Module stores the same graph over `arith.integer<8>` and `fixed.q<8,4>`.
Finally, `edgevec` lowers only `nn.linear` and `nn.relu` to target operations;
the tensor constants and reshape remain ordinary operations in the same Graph.
Optional C++ files attach nonlinear validation, interface methods, and the
lowering implementation. Target and numeric-format policy remain installable
Modules rather than core configuration.

Start with [Build an extension](docs/getting-started.md). See
[the architecture](docs/design.md), [the language reference](docs/language.md),
and the [typed C++ API](docs/cpp-api.md). C++ implementation rules live in
[C++ behavior bindings](docs/bindings.md). Installation, exact lock replay, and
the on-disk layout are specified in [Module packages and locks](docs/packages.md).
Cross-module contracts are described in [Interfaces](docs/interfaces.md), and
the unified pass model in [Passes](docs/passes.md). The shipped IR boundaries
and their relationship to MLIR/TVM terminology are documented in
[IR modules](docs/ir-modules.md).
