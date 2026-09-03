# Joggle

Joggle is a lightweight C++ compiler substrate for AI hardware/software
co-design. It lets a project define its own value types, operators, compiler
functions, analyses, and artifacts without adopting a fixed dialect ladder or
device model.

The design is intentionally small:

- `joggle::Module` is the only top-level object: a versioned symbol scope and
  multi-Function IR owner;
- `fn` is the only callable declaration;
- values are typed independently of whether they are Known to the compiler or
  Residual in a module;
- a second whole-IR container or Graph owner does not exist;
- loading, transformation, analysis, simulation, and emission are ordinary
  typed functions.

There is no language-level `op`, `pass`, `graph`, `region`, `lower`, frontend,
or backend hierarchy. Those can be useful roles in an extension, but the core
does not force their direction or storage model.

```joggle
joggle 1;

module example@1.0.0 {
  type word(width: int);

  fn identity<T: type>(input: T) -> T;

  fn pipeline<N: int>(count: N, input: word<8>) -> word<8> {
    current = input;
    for stage in range(N) {
      current = identity(current);
    }
    return current;
  }
}
```

Here `count: N` binds integer generic `N`, and the ordinary Prelude function
`range(N)` produces a Known list. `for` expands deterministically at compile
time, while each `identity` call may remain as a normal Residual Instruction.
The same body can therefore express compiler decisions and the module they
produce without a second metaprogramming language.

## Build

Joggle requires a C++20 compiler and CMake 3.20 or newer.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /desired/prefix
```

The CLI supports canonical formatting, validation, Module release
installation, locking, and compiler-function pipelines:

```bash
joggle fmt model.joggle --write
joggle check model.joggle --with dependency.joggle
joggle install model.joggle --behavior build/model_behavior.dylib
joggle lock model.joggle -o joggle.lock
joggle run pipeline.joggle compile model.onnx -o model.bin
joggle run pipeline.joggle optimize model.joggle -o optimized.joggle
```

Run `joggle --help` for the complete command forms.

## Language and standard Modules

The language ABI is declared once in the embedded
[`language/prelude.joggle`](language/prelude.joggle). It contains compiler
domains, native scalar types, callable types, core interfaces, and the
deterministic functions needed by dependent types and compile-time control.
It is ambient and cannot be replaced by repository lookup.

[`modules`](modules) contains optional, ordinary installable declarations:

- `arith`: Residual numeric operations, comparisons, selection, and literal
  materialization;
- `tensor`: ranked tensor values and structural operations;
- `nn`: common inference operators and checked shape relations;
- `buffer`: explicit storage values and token-ordered effects.

These Modules are not an ordered lowering stack. Extensions may import and
bridge them in either direction. Prelude's `module` type carries an entire
`joggle::Module` through ordinary compiler functions; it is not another
installable vocabulary.

## Documentation

Start at the [documentation index](docs/README.md). The
[getting-started guide](docs/getting-started.md) builds one extension, the
[language reference](docs/language.md) defines source semantics, and the
[architecture](docs/architecture.md) explains the project boundary. The
[standard Modules](docs/standard-modules.md) and
[compiler-function design](docs/compiler-functions.md) specify the extension
foundation used by optimization, analysis, conversion, and emission.

The [`examples/nn_pipeline`](examples/nn_pipeline) project demonstrates a
source-defined compiler branch, multi-Function transformation, and emitter.
The [`tests/consumer`](tests/consumer) project is the installed-library
integration example.
