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

There is no second source declaration for an Op or pass: every residual `fn`
call is an `Op`, and every compiler transformation is an ordinary typed `fn`.
The core does not force a lowering direction or target storage model.

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
time, while each `identity` call may remain as a normal Residual Op.
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
- `mem`: extensible reference, layout, address-space, alias, and effect
  contracts for storage planning;

These Modules are not an ordered lowering stack. Modules may import and
bridge them in either direction. Prelude's `module` type carries an entire
`joggle::Module` through ordinary compiler functions; it is not another
installable vocabulary. Each Module also owns immutable, content-addressed
binary data, so constants remain part of one compiler artifact without being
expanded into source text or threaded through a side channel.

## Optional Modules

Native Modules use one scalable build selector. For example,
`-DJOGGLE_BUILD_MODULES='onnx;precision'` builds both behaviors; source-only
Modules are always installed. Each package's directory name, DSL name,
installed filename, behavior filename, and CMake target stem are kept aligned.

[`modules/onnx`](modules/onnx) provides an optional, typed ONNX import
Module. `onnx.read` preserves source operations as `onnx.*` IR;
`onnx.to_nn` is a separate transactional conversion to the portable `nn`
vocabulary. Initializer bytes are content-addressed data owned by the returned
Module. Protobuf remains isolated to this Module's native behavior and is not
a dependency of the core library.

[`modules/precision`](modules/precision) provides an f32-to-f16
transformation. It discovers constants through the `tensor.immutable_data`
interface rather than naming their producer, and transactionally updates types
and Module data together.

[`modules/anchor`](modules/anchor) is a concrete vertical slice from typed NN
values to explicit target references, calls, and static scratch placement. Its
layout, storage spaces, deterministic slot reuse, and validated resource
analysis are ordinary Module-owned semantics rather than compiler-core device
classes. The same Module declares an explicit machine type, a deterministic
analytical cycle model, and a portable manifest emitter, so a source-defined
pipeline can run from ONNX bytes to an inspectable deployment artifact without
a parallel pass or backend API. Its elementwise kernels are ordinary Joggle
function bodies over residual target primitives, showing the same `fn`
mechanism on both the compiler and executable sides.

## Documentation

Start at the [documentation index](docs/README.md). The
[getting-started guide](docs/getting-started.md) builds one Module, the
[language reference](docs/language.md) defines source semantics, and the
[architecture](docs/architecture.md) explains the project boundary. The
[standard Modules](docs/standard-modules.md) and
[compiler-function design](docs/compiler-functions.md) specify the Module
foundation used by optimization, analysis, conversion, and emission.

The [`examples/nn_pipeline`](examples/nn_pipeline) project demonstrates a
source-defined compiler branch, multi-Function transformation, and emitter.
The [`tests/consumer`](tests/consumer) project is the installed-library
integration example.
