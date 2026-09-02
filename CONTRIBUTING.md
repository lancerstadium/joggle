# Contributing to Joggle

Joggle is intentionally small. Changes should preserve one canonical text
Module model and the Function/Block/Instruction/Value ownership hierarchy. A
def-use graph or CFG is a non-owning relation over a Function, not another IR
container. New domain concepts normally belong in an installable Module rather
than the core.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Before submitting a change:

1. Format C++ with the repository `.clang-format`.
2. Run `joggle fmt <module> --write` for every changed `.joggle` file.
3. Add a focused unit test and, when a public boundary changes, an end-to-end
   package or installation test.
4. Keep commits reviewable. Separate core mechanics, IR Module changes, and
   documentation when they can be understood independently.

## Compatibility

The `joggle 1;` header versions the text language. Module semantic versions
version extension schemas. Behavior libraries bind to the exact canonical
Module digest and host ABI; changing a Module requires rebuilding its behavior.

Public C++ headers live in `include/joggle`. Implementation-only types stay in
`src`. Do not add generated declaration headers or a second registration API;
the `.joggle` source remains the sole schema authority.
