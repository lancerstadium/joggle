# Tutorial

Build and test Joggle:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Then parse and print a `.jog` file:

```sh
printf 'module hello\n' > hello.jog
./build/joggle hello.jog
```

The equivalent embedded use is:

```cpp
#include <joggle/joggle.h>

int main() {
  joggle::Env env;
  joggle::Mod mod;
  if (!joggle::parse(env, "module hello\n", mod))
    return mod.print_diags(stderr);
  joggle::print(stdout, mod);
}
```

This bootstrap intentionally demonstrates only functionality that exists. The
matrix-multiplication and pass walkthroughs will replace it when their M1 gates
are implemented and tested.
