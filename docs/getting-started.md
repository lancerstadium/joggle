# Getting started

This guide defines a small Mod and transforms its real Fn body with an ordinary
typed equation. Joggle generates no declaration header; `.joggle` source is the
schema and package authority.

## 1. Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

An installed consumer uses `find_package(Joggle CONFIG REQUIRED)`.

## 2. Define a Mod

Create `example.joggle`:

```joggle
joggle 1;

mod example@1.0.0 {
  import transform@2 as tr;

  type word(width: int);

  fn keep<T>(input: T) -> T;
  fn replacement<T>(input: T) -> T;

  fn rewrite(input: fn) -> fn {
    return @tr.pass(
      input,
      (value: word<8>) -> word<8> => keep(value),
      (value: word<8>) -> word<8> => replacement(value)
    );
  }

  fn main(input: word<8>) -> word<8> {
    return keep(input);
  }
}
```

Validate and canonically format it:

```bash
joggle check example.joggle --with /path/to/transform/mod.joggle
joggle fmt example.joggle --write
```

`pass` is an ordinary imported fn called explicitly with `@`. The two lambdas
are ordinary compiler-domain Fns. Their arguments are typed pattern variables;
calls match exact declaration identity and dataflow rather than strings. The
replacement is verified before publication and dead pure producers are removed.

The current equation form uses concrete Types. Generic typed equations are an
active language gate, so this example deliberately states `word<8>` instead of
claiming shape-polymorphic rewriting.

## 3. Run the transformation

```cpp
#include <joggle/joggle.h>

joggle::Compiler compiler;
compiler.load("/path/to/transform/mod.joggle");
compiler.load("example.joggle");
if (!compiler.link() ||
    !compiler.load_native("transform", "/path/to/transform/native")) {
  compiler.diag().print(std::cerr);
  return 1;
}

auto fn = compiler.materialize("example.main");
auto rewritten = fn ? compiler.run<joggle::Fn>("example.rewrite", *fn)
                    : std::nullopt;
if (!rewritten || !compiler.verify(*rewritten)) {
  compiler.diag().print(std::cerr);
  return 1;
}
```

The transformed value is still one `Fn`. Its calls, values, blocks, and nested
lambda bodies were edited directly; there is no graph conversion or pass object.

## 4. Add a native boundary only when needed

Pure declarations and source bodies require no generated C++ wrapper. Host
work such as decoding a file, measuring a device, or writing an object is a
bodyless source fn with an optional native implementation:

```joggle
fn read(input: bytes) -> mod;
```

```cpp
void joggle_mod(joggle::Compiler& compiler, const joggle::Mod& mod,
                joggle::Diag&) {
  compiler.bind(mod, "read",
                [](const joggle::Bytes& input)
                    -> std::optional<joggle::Mod> {
                  return decode(input);
                });
}
```

Build that library with the source Mod as its identity:

```cmake
find_package(Joggle CONFIG REQUIRED)

joggle_mod(example_native
  SOURCE example.joggle
  NATIVE native.cpp
)
```

`joggle_mod` embeds the canonical Mod identity in a hidden build translation
unit. Authors write no export macro or generated declaration header.

## 5. Compose tools

Compiler work remains ordinary source composition:

```joggle
fn compile(input: bytes) -> bytes {
  model = @read(input);
  model = @optimize(model);
  return @emit(model);
}
```

```bash
joggle run driver.joggle compile model.onnx \
  --native build/driver_native.dylib -o model.bin
```

The CLI supports `bytes -> bytes`, `bytes -> mod`, `mod -> mod`, and
`mod -> bytes` entry points. Binary payloads are content-addressed within Mod
bundles and survive `check`, `install`, and `lock`.

See [Language](language.md), [Transform](mods/transform.md), and
[Repository](repository.md) for the complete contracts.
