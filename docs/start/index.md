---
title: Start here
description: Build Joggle and understand the smallest complete program.
---

# Start here

This path builds Joggle, reads one complete `.jog` program, verifies it, and
runs one transformation. It requires CMake 3.20+ and a C++20 compiler.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The default configuration downloads nothing. The CLI is `build/joggle`; built
mods are copied to `build/modules`.

## Read the program

`test/data/matmul.jog` starts with:

```jog
mod test.linear
use tensor

fn add_zero(x: i32) -> i32 {
  let y = x + 0
  return y
}
```

| Line | Meaning |
| --- | --- |
| `mod test.linear` | this file belongs to package `test.linear` |
| `use tensor` | the package depends on the bundled `tensor` mod |
| `fn ... -> i32` | a public function with one typed result |
| `let y` | immutable graph value |
| `x + 0` | typed call resolved through ordinary overloads |

The source is also the readable IR. The call, its result, and the return are
the objects a compiler function observes.

## Check it

```sh
./build/joggle check test/data/matmul.jog -M build/modules > checked.jog
```

`-M` adds a mod search root. `check` parses, resolves, verifies, and prints
canonical `.jog`. A failure reports a source location and publishes no output.

## Transform it

```sh
./build/joggle run opt.fold_add_zero checked.jog \
  -M build/modules > transformed.jog
```

The relevant result becomes:

```jog
fn add_zero(x: i32) -> i32 {
  return x
}
```

The transform redirected the return to `x` and removed the dead addition. It
did not rewrite unrelated functions.

## Inspect without editing

```sh
./build/joggle query opt.untyped transformed.jog -M build/modules
```

Expected output:

```text
[]
```

This means no call result retains the open `_` type. It does not claim
numerical correctness.

## Verify the documented path

```sh
ctest --test-dir build -L tutorial --output-on-failure
```

Continue with [Language](../language/index.md). Do not skip directly to compiler
internals if `mod`, structural types, or function syntax are still unfamiliar.
