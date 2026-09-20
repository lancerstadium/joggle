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
