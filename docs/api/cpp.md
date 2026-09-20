---
title: C++ API
description: Embed environments, parsing, queries, transformations, graph edits, diagnostics, and reactive schedules.
---

# C++ API

Include `joggle/joggle.h`, require C++20, and link `Joggle::joggle`.

## Configure a consumer

```cmake
find_package(Joggle CONFIG REQUIRED)
add_executable(host main.cpp)
target_compile_features(host PRIVATE cxx_std_20)
target_link_libraries(host PRIVATE Joggle::joggle)
```

`install-consumer` tests this installed-package boundary.

## Complete parse–run–print host

```cpp
#include <joggle/joggle.h>

#include <array>
#include <iostream>
#include <span>

int main() {
  joggle::Env env;
  env.path("modules");
  if (!env.load("opt")) {
    env.print_diags(stderr);
    return 1;
  }

  constexpr std::string_view text = R"(
mod demo
use base

fn main(x: i32) -> i32 {
  return x + 0
}
)";

  joggle::Mod mod;
  if (!joggle::parse(env, text, mod, "demo.jog")) {
    env.print_diags(stderr);
    return 1;
  }

  if (!joggle::run(env, "opt.fold_add_zero", mod)) {
    env.print_diags(stderr);
    return 1;
  }

  std::cout << joggle::print(mod);
}
```

`Env` owns package roots, loaded declarations, native bindings, evaluator
caches, and diagnostics. `Mod` owns one mutable graph. Destroy handles before
their owning graph.

## Parse forms

```cpp
joggle::Mod mod;
joggle::parse(env, source_text, mod, "input.jog");

std::array<joggle::Source, 2> sources{{
    {entry_text, "module.jog"},
    {helper_text, "lib/helper.jog"},
}};
joggle::parse(env, sources, mod);

joggle::Attr config;
joggle::parse(env, "{limit: 8, mode: \"fast\"}", config, "config");
```

Multi-source parsing preserves each filename in diagnostics. Attribute parsing
uses the same grammar as CLI `--arg` and metadata.

## Load and resolve functions

```cpp
env.path("build/modules");
env.path("examples/mods");
env.load("opt");

joggle::Fn exact = env.find_fn("opt.basic");
std::vector<joggle::Fn> family = env.find_fns("opt.fold");
std::vector<std::string> loaded = env.modules();
```

Use `resolve`/`resolve_fns` when lookup must respect a particular `Mod` or
calling `Fn`. Use `match`, `accepts`, and `expand` for typed implementation
selection instead of comparing signature strings.

## Query

```cpp
joggle::Attr result;
joggle::QueryReport report;
std::array<joggle::Attr, 1> args{joggle::Attr("nn.relu")};

if (!joggle::query(env, "opt.count", mod, result, args, &report)) {
  env.print_diags(stderr);
  return 1;
}

auto count = result.integer();
if (!count) return 1;
```

`QueryReport` tells whether the answer was cached, why a lookup missed, what it
observed, and how long lookup/snapshot/verification/evaluation took.

## Run one or several functions

```cpp
joggle::Attr report;
joggle::RunTiming timing;
std::array<std::string_view, 2> stages{
    "opt.fold_add_zero", "opt.basic"};

if (!joggle::run(env, stages, mod, report, {}, timing)) {
  env.print_diags(stderr);
  return 1;
}
```

The sequence is one transaction. `RunTiming.steps` records per-stage revisions,
verification reuse, evaluator counters when enabled, and phase durations.

## Inspect graph handles

```cpp
for (joggle::Fn fn : mod.fns()) {
  if (!fn.live() || fn.local()) continue;
  for (joggle::Blk block : fn.blks()) {
    for (joggle::Op op : block.ops()) {
      for (joggle::Val value : op.outs()) {
        if (!value.live()) continue;
        std::cout << value.type().text() << "\n";
      }
    }
  }
}
```

Handles are lightweight references into a store. Structural edits can make a
handle non-live; never assume an earlier index still denotes the same object.

## Edit through `Mod`

| Goal | API |
| --- | --- |
| insert computation | `call`, `constant`, `assign`, `branch`, `clone` |
| redirect uses | `replace(Val, Val)` or one-user overload |
| remove | `erase(Op)` or batch `erase(span<Op>)` |
| refine types | `type`, batch `type`, `returns`, `generics` |
| rename/select | `rename`, `retarget` |
| annotate | `set`/`unset` for `Fn`, `Op`, `Val` |
| finish | `verify(env)` |

Example metadata edit:

```cpp
joggle::Op call = /* selected live call */;
if (!mod.set(call, "implementation", joggle::Attr("lut"))) return 1;
if (!mod.verify(env)) {
  env.print_diags(stderr);
  return 1;
}
```

Prefer batch overloads after collecting targets. They validate the complete
request and avoid repeated graph traversal.

## Types and attributes

```cpp
joggle::Ty tensor("tensor<f32, [2, 4]>");
if (!tensor.valid() || tensor.name() != "tensor") return 1;

joggle::Attr::Dict policy{
    {"limit", joggle::Attr(std::int64_t{8})},
    {"mode", joggle::Attr("fast")},
};
joggle::Attr config(std::move(policy));
```

`Ty` is a structural tree. `Attr` is a variant of nil, Boolean, integer, real,
string, bytes, list, and ordered dictionary. Accessors return optionals or
pointers so a host must check runtime kind.

## Diagnostics

```cpp
for (const joggle::Diag& diag : env.diags()) {
  std::cerr << diag.loc.file << ':' << diag.loc.line << ':'
            << diag.loc.column << ": " << diag.message << '\n';
}
env.clear_diags();
```

Preserve `Loc`; do not replace compiler diagnostics with a generic host error.

## Reactive schedule

```cpp
joggle::ReactiveSchedule schedule({"opt.basic", "mem.plan"});
joggle::ReactiveRunReport first;
joggle::ReactiveRunReport next;

if (!schedule.run(env, mod, {}, &first)) return 1;
// Apply an authorized edit to mod.
if (!schedule.run(env, mod, {}, &next)) return 1;

std::cout << next.executed_stages << " executed, "
          << next.reused_stages << " reused\n";
```

Keep the schedule when the stage list is stable and the graph evolves. Call
`reset()` for an explicit cold run; construct a new schedule for a new list.

## Version boundaries

`version_major/minor/patch` describe the C++ release. The native ABI has a
separate `abi_version`. Joggle is pre-1.0, so compile host and library against a
matching documented release rather than relying on compatibility shims.
