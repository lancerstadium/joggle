# Vertical evolution study

This protocol tests the paper's primary consequence: whether one co-design
concept can change across representation, policy, target binding, and artifact
construction without repeatedly coordinating unrelated host extension
lifecycles. It is not a usability study and does not measure developer
productivity.

The task contract is frozen in [`tasks/evolution.json`](../tasks/evolution.json).
Implementations and measurements must not weaken a stage after a system result
is known.

## Research question and unit of comparison

**Question.** After a correct external-convolution path exists, which
framework-owned boundaries must be revisited when the target first changes its
weight representation and then changes its epilogue ABI?

The unit of comparison is one evolving specialization, not three independent
feature tasks. Every system receives the same ONNX graphs, tensors, native
kernels, stage descriptions, correctness oracle, and artifact-entry contract.
Joggle, TVM/Relax--TensorIR, and ONNX-MLIR must use their documented native
extension paths. A result is comparable only after it produces the required
callable artifact or records the first unsatisfied mandatory requirement.

## Frozen workload

Both inputs are ordinary ONNX `Conv -> Add -> Relu` graphs with NCHW
activations, constant OIHW weights, broadcast bias, unit stride, and one-element
padding. Bias is an explicit `Add`, rather than the optional `Conv` input, so S0
does not favor ONNX-MLIR's fused `Conv` call boundary over TVM's decomposed
Relax graph. The eligible case uses input `[1,4,5,5]`, weight `[4,4,3,3]`, bias
`[1,4,1,1]`, and output `[1,4,5,5]`. The fallback case uses input
`[1,3,5,5]`, weight `[4,3,3,3]`, the same bias shape, and the same output
spatial shape. Fixture generation uses a fixed seed and records model, input,
weight, bias, and expected-output digests.

The source graphs do not change between stages. The fallback graph is not an
unsupported corner case: it proves that a target-specific representation does
not replace the portable semantic path.

## Sequential stages

### S0: external convolution

Bind the supplied row-major NCHW/OIHW convolution kernel. Bias addition and
ReLU remain in the framework's portable path. Both shapes must produce callable
native artifacts and pass the common oracle. This stage freezes each system's
initial implementation before either revision is revealed.

### S1: packed-weight revision

The target now consumes constant weights in `OIHW2` order
`[M,Q/2,R,S,2]` and accepts only even `Q`. The compiler must materialize the
packed payload, select the external kernel for the eligible graph, and preserve
the portable fallback for odd `Q`. It may not modify the ONNX graphs, pre-pack
the checked-in fixture, or silently pad the fallback case. The artifact must
contain or reference the packed payload derived from the represented program.

### S2: fused-epilogue ABI revision

The packed kernel ABI gains a bias pointer and an activation selector. For the
eligible graph, the external call must implement convolution, bias, and ReLU as
one selected target implementation. The odd-channel graph must continue through
the portable path. The source graphs and numerical oracle remain unchanged.
The artifact interface presented to the application must be derived or checked
by the system rather than reconstructed from an unrecorded handwritten model
signature.

## Boundary accounting

The primary observation is a named boundary vector, not source lines, elapsed
authoring time, or a weighted extensibility score. Before implementation, every
framework-owned edit is classified into exactly one of these domains:

1. source relation or frontend recognition;
2. portable semantic definition;
3. representation, type, or payload materialization;
4. structural transformation or selection policy;
5. target/external-call binding;
6. artifact ABI or runtime bridge; and
7. host registration or build wiring.

A domain is *revisited* only when stage-specific framework-authored source in
that domain changes. Common fixtures, supplied kernel revisions, generated
files, build outputs, and oracle code are reported but excluded from the
framework boundary vector. Files and nonblank lines are retained as audit data,
never interpreted as effort.

For each revisited domain, the record names the independently checked or built
contract that makes it a separate coordination boundary. Two files governed by
one module validation event count as one boundary; one file participating in
two independently validated interfaces counts in both, with the reason shown.
The paper presents the vector and names, so a reviewer can reject the
classification without trusting an aggregate score.

The domain of an edit follows the interface it implements, not whether its
file lives in framework core. A compiler-core conversion change to an
external-call ABI belongs to domain 6; its core-source modification and host
rebuild are recorded separately. Domain 7 is reserved for registration and
build wiring. Describing ordinary row-major input buffers and their stride
arguments is target ABI work, not a new representation in domain 3. An authored
selection mutator counts in domain 4 even when S0 selects both shapes.

These adjudications were applied in a **pre-freeze S0 audit on 2026-09-15**
after functional macOS preflight, because the initial-surface metadata
inconsistently assigned TVM's `ReplaceConv` and ONNX-MLIR's `KrnlCall.cpp`.
The changes are disclosed in the S0 records. They alter no source graph,
artifact, oracle result, or S0 revisited-boundary vector (which is empty for
every system); all S1/S2 classification must use this rule prospectively.

## Required observations per stage

Each system record contains:

- exact system revision, build configuration, toolchain, and hardware;
- patch against the frozen preceding stage and hashes of authored inputs;
- named revisited-boundary vector with file and interface evidence;
- host rebuild, extension rebuild, and artifact rebuild scopes and times;
- selected versus fallback path, preserved IR or generated source, and symbols;
- numerical error for both graphs and digest of the callable artifact;
- peak compiler RSS and end-to-end compiler invocation time;
- first diagnostic for one deliberately mismatched packing factor or ABI; and
- whether the prior-stage artifact and fallback still pass unchanged.

Build time is machine cost, not idea-to-artifact or developer time. Wall-clock
authoring time is not reported because the same authors implement all systems
and cannot supply independent usability evidence.

## System paths

- **Joggle:** the extension depends on the shared ONNX relation and portable
  `nn.conv2d` semantics, and owns the concept-specific packing/materialization,
  applicability predicate, and selected external declaration. The generic C
  artifact module must derive payloads and interfaces from the resulting
  program. Native kernel source remains a supplied task input. No new host
  primitive, operator case, or C-emitter symbol case is permitted.
- **TVM:** use Relax/ONNX import, TensorIR or documented external-codegen calls,
  runtime modules, and the ordinary artifact export path. Python glue is
  allowed when it is the documented user extension surface; modifications to
  TVM source or registration/build files are recorded, not forbidden.
- **ONNX-MLIR:** use the documented `Conv` external-call path or an
  accelerator-scoped dialect/pass when required, followed by the normal native
  artifact route. Compiler, generated-definition, pass-registration, type
  conversion, runtime, and build edits are all part of the observed boundary
  vector.

The earlier ONNX-MLIR generic-`MatMul` unsupported result does not answer this
study: `Conv` has a documented external-call route and must be tested directly.

## Fairness and failure rules

- All systems consume byte-identical ONNX and tensor fixtures.
- No system may replace the portable fallback with a reference-runtime call.
- System-specific runtime wrappers are allowed only when required by the normal
  public artifact API; handwritten model semantics are not.
- A documented absence confirmed by emitted IR and source inspection is
  `unsupported`. A build failure or unfinished implementation is `incomplete`,
  not `unsupported`.
- A timeout records its stage, elapsed time, last diagnostic, and retained
  artifact. It does not become an empty or passing cell.
- Performance of the tiny fixture is not used as a generated-code comparison;
  this study measures evolution boundaries and artifact closure.

## Presentation contract

The main-paper result is one dense three-system by three-stage figure. Each
cell shows revisited-boundary glyphs, build scope, diagnostic location,
selected/fallback status, and oracle status. Exact files, patches, times, hashes,
and error values remain in the artifact. The text may claim fewer revisited
boundaries only if the named vectors support it; it may not claim lower human
effort, general usability, or universal extensibility.

Until all S0 implementations are frozen, the manuscript continues to label
lower revision-coordination cost as unproven.

## Preflight status

The deterministic eligible and fallback fixtures are generated and hash-checked
under `fixtures/evolution/`. Cross-system preflight must confirm the neutral
`Conv -> Add -> Relu` decomposition before S0 is frozen. Earlier preflight on
the optional-`Conv`-bias form exposed a framework-specific decomposition and
was discarded before cross-system stage implementation; it is not a study
result. The regenerated fixtures now pass all three import paths. Joggle
imports one `nn.conv2d`, one `nn.add`, and one `nn.relu`; TVM Relax imports one
`R.nn.conv2d`, one `R.add`, and one `R.nn.relu`; ONNX-MLIR retains one each of
`onnx.Conv`, `onnx.Add`, and `onnx.Relu`, and its documented
`--ops-for-call=Conv` route produces exactly one `krnl.call` whose operands are
the output, activation, and weight rather than the explicit bias. These are
preflight observations, not S0 comparison results.

Joggle's S0 functional preflight closes callable C artifacts for both shapes.
Each prepared program contains one external convolution call, no remaining
`nn.*` calls, and four ordinary loops implementing the portable broadcast,
addition, and ReLU path. Both artifacts match the frozen oracle with zero
maximum absolute error; their source hashes and artifact digests are recorded
in [`evolution/joggle/s0/result.json`](../evolution/joggle/s0/result.json).

ONNX-MLIR's normal `--ops-for-call=Conv` route initially stopped at
Krnl-to-LLVM conversion because its `krnl.call` attribute handler did not
accept the integer arrays copied from Conv's pads and strides. A preserved
host patch makes that attribute ABI explicit, and a system-specific OMTensor
bridge links the same supplied convolution kernel through `-L`/`-l`. Both
normal `--EmitLib` artifacts preserve portable affine Add and ReLU operations
and match the 100-element oracle bitwise. The patch, ABI, first diagnostic,
and digests are in
[`evolution/onnx-mlir/s0/result.json`](../evolution/onnx-mlir/s0/result.json).
TVM's pinned ONNX-to-Relax path now builds both graph shapes with one Conv
replaced by `call_tir` to a TensorIR external-kernel wrapper; Add and ReLU
remain ordinary Relax calls. The LLVM-enabled build exports native libraries
containing the supplied convolution object. Both libraries reload in fresh
processes and match the 100-element oracle bitwise. Imported/selected IR,
artifact digests, and the initial extension surface are preserved in
[`evolution/tvm/s0/result.json`](../evolution/tvm/s0/result.json). All three
systems therefore pass functional S0 preflight on macOS, but S0 remains
unfrozen until matched Linux artifacts and measurement protocol checks pass.
No smoke-test compiler or inference time is a cross-system result.
