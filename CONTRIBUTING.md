# Contributing to Joggle

Joggle is intentionally small. Changes should preserve one canonical text
Mod model and the Fn/Blk/Op/Val ownership hierarchy. A
def-use graph or CFG is a non-owning relation over a Fn, not another IR
container. New domain concepts normally belong in an installable Mod rather
than the core.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Before submitting a change:

1. Format C++ with the repository `.clang-format`.
2. Run `joggle fmt <mod> --write` for every changed `.joggle` file.
3. Add a focused unit test and, when a public boundary changes, an end-to-end
   package or installation test.
4. Keep commits reviewable. Separate core mechanics, IR changes, and
   documentation when they can be understood independently.

## Compatibility

The `joggle 1;` header versions the text language. Mod semantic versions
version Mod schemas. Native libraries bind to the exact canonical
Mod digest and host ABI; changing a Mod requires rebuilding its native
library.

Public C++ headers live in `include/joggle`. Implementation-only types stay in
`src`. Do not add generated declaration headers or a second registration API;
the `.joggle` source remains the sole schema authority.
