# Joggle

Joggle is a small C++20 compiler workbench for neural-network and hardware/
software co-design research. It gives experiments one readable IR, one module
format, and one extension boundary without prescribing a target, scheduler, or
paper mechanism.

The project is being rebuilt from a deliberately small foundation. The core
contains no ONNX, device, instruction-set, runtime, or code-generation policy.
Those capabilities belong in removable modules.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The core and command-line tool require only a C++20 compiler and the standard
library. Building does not download dependencies.

## Shape of the project

- `joggle::Env` owns loaded modules, native bindings, and environment
  diagnostics.
- `joggle::Mod` owns one self-contained IR unit.
- `Fn`, `Blk`, `Op`, and `Val` are stable handles into a `Mod`.
- `Ty` and `Attr` are structural values.
- `.jog` is the only source and readable IR format.
- imports, transforms, analyses, simulators, and emitters are module functions,
  not separate plugin class families.

Start with [the design](docs/design.md), then read the
[language](docs/language.md), [module model](docs/modules.md), and
[tutorial](docs/tutorial.md).

The implementation removed during the redesign remains recoverable at Git tag
`archive/pre-relaunch-a2a281e`.

## License

MIT. See [LICENSE](LICENSE).
