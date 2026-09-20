---
title: CLI
description: Inputs, outputs, arguments, diagnostics, and contracts for every command family.
---

# CLI

## File and mod commands

| Command | Input | stdout | May edit input graph |
| --- | --- | --- | --- |
| `check FILE` | `.jog` | canonical `.jog` | no |
| `read FN FILE` | bytes | decoded `.jog` | creates new graph |
| `query FN FILE` | `.jog` | canonical `Attr` | no |
| `run FN... FILE` | `.jog` | revised `.jog` | transactionally |
| `emit FN FILE` | `.jog` | text or bytes | no |
| `mod list/info/check/...` | roots/package | administrative text | package lifecycle only |

All file commands accept `-` for stdin. Keep named intermediates when a stage
must be inspected or reproduced.

## Search roots

```sh
joggle run policy.apply model.jog \
  -M build/modules -M examples/mods
```

Each `-M` is a package root. `use` declarations still determine dependencies;
a root does not import everything beneath it.

## Typed arguments

```sh
joggle query opt.count model.jog \
  --arg '"nn.relu"' -M build/modules

joggle run locality.block model.jog \
  --arg '[4, 7]' --arg 4000 \
  -M build/modules -M examples/mods
```

Each `--arg` is parsed as `Attr` syntax and matched against the function
signature. Strings therefore include their Joggle quotes.

## Reports

```sh
joggle run opt.basic model.jog \
  --report run.attr --timing timing.attr \
  -M build/modules > optimized.jog
```

The report is deterministic structural data; timing is variable measurement.

## Diagnostics

Place `--diagnostics` before the command:

```sh
joggle --diagnostics jog check broken.jog -M build/modules
```

Structured stderr has canonical `list<dict<str, Attr>>` form:

```text
[{"column": 4, "file": "broken.jog", "line": 9,
  "message": "unresolved function: missing", "severity": "error"}]
```

Never parse human diagnostic prose in automation.

## `check`

```sh
./build/joggle check input.jog -M build/modules > canonical.jog
```

`check` parses, loads declared dependencies, resolves and verifies the graph,
then prints canonical source. It never modifies the input file. The output can
be parsed again; this makes it suitable for a formatting/validation boundary.

```sh
./build/joggle check - -M build/modules < input.jog > canonical.jog
```

Use stdin only when the producer is already deterministic. A named input gives
better filenames in diagnostics.

## `read`

```sh
./build/joggle read onnx.read model.onnx \
  -M build/modules > decoded.jog
```

`read` passes raw bytes to a `[role: "read"]` function and expects `.jog` text.
It is the binary-format boundary. Always follow it with `check` before treating
the output as a verified graph.

## `query`

```sh
./build/joggle query opt.count model.jog \
  --arg '"nn.relu"' -M build/modules
```

The entry must be a non-generic function whose first parameter is `Mod` and
whose remaining parameters match the supplied `Attr` arguments. It runs
read-only. A mutation attempt is rejected and the source graph remains
unchanged.

Typical query outputs:

```text
7
[]
{"calls": 18, "functions": 3}
```

They use canonical `Attr` syntax, not JSON. Booleans and strings look familiar,
while byte strings use `hex"..."` and type-like data may remain source terms.

## `run`

```sh
./build/joggle run source.infer source.convert opt.basic model.jog \
  -M build/modules > optimized.jog
```

Functions run left-to-right in one transaction. Initial verification happens
before the first stage; final verification happens before commit. A stage
failure prints no partially revised graph.

`--report` writes deterministic execution evidence:

```text
{
  "after": 3,
  "before": 0,
  "changed": true,
  "edits": 3,
  "ok": true,
  "reported": true,
  "steps": [...]
}
```

`--timing` writes profiling data in the same `Attr` data model. Durations are
nanoseconds and counters may be zero unless the build enabled evaluator
instrumentation.

## `emit`

```sh
./build/joggle emit c.source planned.jog \
  -M build/modules > model.c
```

An emitter is a read-only function returning `str` or `bytes`. Text is written
without an added wrapper; bytes use binary stdout on Windows. Query a target's
frontier before emission so an unsupported call is diagnosed at the capability
boundary rather than inside generated code.

## Mod lifecycle commands

| Command | Purpose | Mutated path |
| --- | --- | --- |
| `mod list` | discover packages in roots | none |
| `mod info NAME` | print public signatures/metadata | none |
| `mod check NAME` | load and verify package/dependencies | none |
| `mod install DIR ROOT` | stage, validate, publish a package copy | destination root |
| `mod upgrade DIR ROOT` | validate replacement and reverse dependencies | destination package |
| `mod uninstall NAME ROOT` | remove installed package if allowed | destination package |

```sh
./build/joggle mod info nn -M build/modules
./build/joggle mod check choose_lut \
  -M build/modules -M examples/mods
```

`mod info` is the authoritative installed signature inventory. The website
adds explanation and examples; it does not replace runtime introspection.

## Argument routing

All `--arg` values are passed to every named run stage. Therefore a multi-stage
command should use compatible argument signatures. If stages need unrelated
configuration, expose one orchestration function with a documented config
dictionary or run explicit transactions at the embedding layer.

```sh
./build/joggle run project.pipeline model.jog \
  --arg '{target: "c", vector_width: 4}' \
  -M build/modules -M local-mods
```

## Streams and exit behavior

| Stream/status | Contract |
| --- | --- |
| stdout | successful canonical graph, query result, or artifact |
| stderr | diagnostics and command errors |
| exit `0` | requested contract completed |
| nonzero | no valid stdout artifact should be consumed |

Write output to a temporary file and rename it only after a zero exit when a
build system needs atomic artifact publication.

## Reproducible shell pattern

```sh
set -eu
root=build/pipeline
mkdir -p "$root"

./build/joggle check input.jog -M build/modules > "$root/checked.jog"
./build/joggle run opt.basic "$root/checked.jog" \
  --report "$root/run.attr" --timing "$root/timing.attr" \
  -M build/modules > "$root/optimized.jog"
./build/joggle check "$root/optimized.jog" \
  -M build/modules > "$root/final.jog"
```

## Failure guide

| Failure | Inspect first |
| --- | --- |
| unknown option/odd option count | command syntax and option placement |
| `--arg` parse error | quote it as Joggle `Attr`, especially strings |
| entry not found | qualified name and `mod info` |
| entry signature mismatch | first `Mod`, remaining argument types, result contract |
| no output after failure | stderr structured diagnostics; partial stdout is invalid |
| wrong package chosen | ordered `-M` roots and installed package names |
