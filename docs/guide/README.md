# Tutorial

Build and test Joggle:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Then verify and canonically print the real matrix-multiplication fixture:

```sh
./build/joggle check test/data/matmul.jog -M modules
```

Automation can select canonical structured failures without changing successful
output or learning another serialization format:

```sh
./build/joggle --diagnostics jog check test/data/matmul.jog -M modules
```

The failure value is an attribute list on standard error. Entries always carry
`severity` and `message`; source-backed failures also carry `file`, `line`, and
`column`.

Run the textual add-zero transform and print its result:

```sh
./build/joggle run opt.fold_add_zero test/data/matmul.jog -M modules
```

Keep the transformed IR on standard output and write deterministic execution
evidence separately when an experiment needs it:

```sh
./build/joggle run opt.fold_add_zero test/data/matmul.jog \
  --report run.attr --timing run-timing.attr -M modules
```

For a temporary experiment, place several ordinary functions before the input
instead of creating a wrapper module:

```sh
./build/joggle run opt.fold_add_zero opt.basic test/data/matmul.jog \
  --report run.attr -M modules
```

The sequence commits once. If any function fails, none of its edits are
printed and the report is not written.
Each step in the report includes deterministic `calls` counts for its source
functions and `cached` counts for `[memo]` hits. Use them to find repeated
policy or analysis work. The separate timing file records whether a full
structural snapshot was materialized, rollback-state preparation, initial
verification, resolution, evaluation, and commit verification without
making the structural report nondeterministic.

Use `-` wherever a command expects an input file to compose processes without
inventing a pipeline object or temporary IR files:

```sh
./build/joggle run opt.fold_add_zero test/data/matmul.jog -M modules |
  ./build/joggle query opt.untyped - -M modules
```

`read` accepts binary standard input as well, while `emit` writes byte
artifacts unchanged. Keep named intermediate files when the progressive states
are evidence that an experiment must retain.

Run a read-only analysis without rewriting or reprinting the module:

```sh
./build/joggle query opt.untyped test/data/matmul.jog -M modules
```

The result is a canonical `Attr` list of call names whose outputs still have
the open `_` type. `opt.unresolved` is the separate symbol-visibility frontier.
For every file command, the CLI loads the dependency closure declared by the
model's `use` lines from the supplied module paths before verification. A
frontend-produced model can therefore be checked, queried, transformed, or
emitted without manually naming each transitive dependency.

Inspect the modules available on the same explicit search path:

```sh
./build/joggle mod list -M modules
./build/joggle mod info nn -M modules
./build/joggle mod check nn -M modules
```

An external module needs only its directory. Install, upgrade, and remove it
from an explicit local root as follows:

```sh
./build/joggle mod install path/to/my.module local-modules -M modules
./build/joggle mod upgrade path/to/my.module local-modules -M modules
./build/joggle mod uninstall my.module local-modules
```

Install validates a staged copy before it becomes visible and does not replace
an existing directory. Upgrade retains every installed signature modulo
generic-parameter names, validates new dependencies and native code in staging,
and restores the prior directory if commit fails.

The equivalent embedded use is:

```cpp
#include <joggle/joggle.h>

int main() {
  joggle::Env env;
  joggle::Mod mod;
  constexpr std::string_view source =
      "mod demo\nfn id(x: i32) -> i32 { return x }\n";
  if (!joggle::parse(env, source, mod, "model.jog"))
    return mod.print_diags(stderr);
  if (!mod.verify(env))
    return mod.print_diags(stderr);
  joggle::print(stdout, mod);
}
```

The complete tested workflow is in `test/workflow.cpp`. It loads `base` and
`tensor`, parses and verifies the generic nested-loop matmul, round-trips its
canonical form, and walks the same `Fn/Blk/Op/Val` representation to fold
`x + 0` by replacing uses and erasing the call. The test also loads an actual
native module and calls its declared native function.

There is no hidden lowering step in this workflow. Loops, calls, mutable source
bindings, and function edits all refer to one `Mod`.
