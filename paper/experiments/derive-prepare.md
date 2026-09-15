# Real-model compiler-procedure derivation pilot

This is a local mechanism check, not a competitive performance experiment.
The procedure is the existing source-defined `c.prepare(Mod)`. It is cloned
with its private lexical call closure into a separate `Mod`; the second
`opt.expose` call in the copied private `prepare_with` is replaced by `false`.
The original definition and model inputs remain separate. The runner
compares the prepared IR and `c.source` byte-for-byte and verifies both models.
It does not establish that this edit is legal for arbitrary programs.

Build and run from the repository root:

```sh
cmake -S . -B build
cmake --build build --target joggle-derive-prepare -j2
./build/joggle-derive-prepare build-study/c-current/semantic.jog modules build/modules
./build/joggle-derive-prepare build-study/tflite-app/semantic.jog modules build/modules
./build/joggle-derive-prepare build-study/ultraface-block/canonical.jog modules build/modules
./build/joggle-derive-prepare build-study/squeezenet-block/canonical.jog modules build/modules
```

The inputs are existing, ignored complete-model IR artifacts, not checked-in
synthetic fixtures. The UltraFace and SqueezeNet inputs are already canonical
intermediates from prior studies, whereas the two MobileNet inputs are semantic
IR. They must be regenerated or transferred
with their provenance before an external reproduction. The input and compiler
source hashes for this run are:

| Artifact | SHA-256 |
| --- | --- |
| ONNX MobileNetV2 semantic IR | `7a9a1d442068d5d346109808fca0b15ac77a453ad649289ea3b488d5821f93e3` |
| TFLite MobileNetV2 semantic IR | `b39116180618efd8a81889824e8ccd081a77d5a53efbb2fa844409235a8ee4c8` |
| UltraFace canonical IR | `7340007dba3d075ca56d4cac9cd89bd60583fcad195999c8118243de18caf891` |
| SqueezeNet canonical IR | `d1e77c052437dc41f19f730a0a672d61e63be20abbdd8bffc8ae1c798ace768d` |
| `modules/c/module.jog` | `c82e0ddd3386acf37691776a43915ab889ab7947624c08d94b90f4f4ed9bfbb3` |
| Pilot runner | `7d5d8ac1c12193f63bfaad1b1e69bbfcbc9cfefaeaf4d7342f2196e444119abe` |

Environment: macOS arm64, Apple M4, unisolated interactive host; repository
base `b1d8bba` plus the runner in this change. Wall times below are diagnostic
only. The host had other work running, the run order was original then derived,
and there is neither randomized ordering nor dispersion. Do not convert these
numbers into an acceleration claim.

| Input | Original / derived prepare | Model revision, both | IR bytes each | Checked result |
| --- | ---: | ---: | ---: | --- |
| ONNX MobileNetV2, 28,413,332 bytes | 24.640 / 24.092 s | 0 → 7,593 | 28,504,666 | identical IR/C source, both verified |
| TFLite MobileNetV2, 27,955,752 bytes | 32.263 / 31.275 s | 0 → 9,866 | 28,056,370 | identical IR/C source, both verified |
| UltraFace, 2,675,643 bytes | 4.595 / 4.043 s | 0 → 7 | 2,674,735 | identical IR/C source, both verified |
| SqueezeNet, 9,978,320 bytes | 1.702 / 1.548 s | 0 → 7 | 9,977,940 | identical IR/C source, both verified |

The compiler copy took 0.004 s in each reported run. Its code module contains
20 functions and 1,115 operations; the edit moved its revision from 20 to 21.
These counts measure the copied source closure, not the whole installed
compiler or runtime memory. All four model procedures have nonzero structural
revision growth. A revision increment is not necessarily one independent
optimization action. The observed equality on three network architectures
may mean the second exposure step was redundant for these inputs; it does not
prove a general rewrite law or useful speedup. Next: seek an input on which
post-inference exposure actually performs edits and compare a useful legal
derivation with the nearest mechanism control in a matched environment.

## Phase boundary probe

The source-only [exposure module](exposure/module.jog) executes the first
`opt.expose`, `ir.type`, and second `opt.expose` from the existing
private `prepare_with` body in `c`. It returns the second call's `bool` result.
The module
has SHA-256
`8b94d812472a37e83b75bd13defb95e199264e9c490be378e04782255673b58a`.
To preserve a report for any input above:

```sh
./build/joggle module check exposure -M paper/experiments -M modules -M build/modules
./build/joggle run exposure.phases build-study/c-current/semantic.jog \
  --report build-study/derivation/onnx-phases.jog \
  -M paper/experiments -M modules -M build/modules > /dev/null
jq '{before,after,expose_calls:.calls["opt.expose"],
     expose_steps:[.steps[]?|select(.fn=="opt.expose")|
                   {before,after,edits}]}' \
  build-study/derivation/onnx-phases.jog
```

The same command with the other three input and report paths was executed
on the macOS host above. Each report records two `opt.expose` calls but only
one changed `opt.expose` step. The first changed step moves the model revision
from 1 to 2,011 (ONNX MobileNetV2), 1 to 3,233 (TFLite MobileNetV2), or 1 to 7
(UltraFace and SqueezeNet). The final report revision is respectively 2,011,
3,234, 7, and 7. The TFLite revision increase between its first exposure and
the final report is attributable to `ir.type`; its second exposure still
reports no structural edit. Unchanged calls are omitted from the `steps`
array, so the separate `calls` count is necessary to establish that the second
exposure actually ran.

This probes one concrete edit, not the general safety of omitting a
post-inference exposure. `ir.type` can change inferred value types and
normalization; a capability predicate may then make a new call eligible for
exposure. Until a rewrite condition or counterexample search closes that
boundary, the original `c.prepare` retains both phases.
