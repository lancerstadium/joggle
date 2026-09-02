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
  import bitmath@1 as math;

  graph example(%lhs: math.word<8>, %rhs: math.word<8>) -> math.word<8> {
    %sum = math.add(%lhs, %rhs);
    return %sum;
  }
}
```

The imported `bitmath` Module declares `word` and `add` once. The graph uses
those names directly. Authors defining a new hardware or numeric extension use
the same Module surface to add declarations and passes.

```cpp
joggle::Compiler compiler;
compiler.load("bitmath.joggle");
compiler.load("model.joggle");

if (!compiler.link()) {
  compiler.diagnostics().print(std::cerr);
  return 1;
}

auto bitmath = compiler.module("bitmath");
auto word = bitmath->type("word");
auto i8 = compiler.make(*word, 8);
auto width = i8->get<std::int64_t>("width");

auto graph = compiler.graph("model.example");
```

```bash
joggle fmt module.joggle --write
joggle check examples/feedforward.joggle \
  --with examples/bitmath.joggle --with examples/fixed.joggle \
  --with examples/miniai.joggle
joggle run module.joggle main optimize -o optimized.joggle
joggle install examples/bitmath.joggle
joggle install examples/fixed.joggle
joggle install examples/miniai.joggle
joggle install examples/feedforward.joggle
joggle install examples/edgevec.joggle
joggle install module.joggle --behavior build/behavior.dylib
joggle uninstall bitmath@1.0.0
joggle lock examples/feedforward.joggle -o joggle.lock
```

The shipped examples form one vertical extension rather than unrelated demos.
`bitmath` defines the numeric-format contract, `fixed` implements a third-party
format, and `miniai` defines format-independent tensor operations. The
`feedforward` Module contains the same model over `word<8>` and `q<8,4>`; its
reshape result is inferred directly from `shape = [1, 4]`, without repeating a
result annotation.
Finally, `edgevec` lowers either graph to lane-aware operations and exposes its
cycle meaning through its own interface. Optional C++ files attach only domain
checks, interface methods, and the lowering implementation. Target and numeric
format policy remain ordinary extension Modules rather than core configuration.

Start with [Build an extension](docs/getting-started.md). See
[the architecture](docs/design.md), [the language reference](docs/language.md),
and the [typed C++ API](docs/cpp-api.md). C++ implementation rules live in
[C++ behavior bindings](docs/bindings.md). Installation, exact lock replay, and
the on-disk layout are specified in [Module packages and locks](docs/packages.md).
Cross-module contracts are described in [Interfaces](docs/interfaces.md), and
the unified pass model in [Passes](docs/passes.md).
