# Agent-authoring study: can an agent reach a validated compiler decision?

The manuscript motivates malleable compilation partly by automated agents that
can already write a compiler decision but cannot cheaply try one beside the
installed implementation. That motivation is currently an argument, not a
measurement. This study tests it.

## Pre-registered protocol

Written before any run. Registered here so the analysis cannot select a
favourable subset afterwards.

**Question.** Given a decision to change inside a compiler, does an agent reach
a *validated* change, and at what cost, when the decision is an editable and
checked source definition (arm A) versus when it lives in a separately
maintained implementation that must be rebuilt (arm B)?

**Arms.** An agent-versus-compiler comparison is only meaningful against the
systems the paper already measures, so the arms are the three compilers, not a
generic C++ stand-in.
- **A, Joggle.** The decision is a definition in a loaded `.jog` module. The
  agent edits the module and re-runs the query; nothing is rebuilt, and the
  installed original stays callable.
- **B, TVM.** The same decision as it exists in TVM's source tree. The agent
  edits C++ and relinks.
- **C, ONNX-MLIR.** The same decision as it exists in ONNX-MLIR's source tree.
  The agent edits and rebuilds.

All three source trees are already built on this host, so no arm is penalised
for a cold start. A decision that a system does not expose is recorded as *not
expressible* for that arm rather than as a failure, because reach is itself part
of the comparison.

**Reference metrics.** The field's recognised measures for this kind of claim
are task success under the system's own verifier, time to a validated change
including all rebuilds, numerical agreement against a stored reference, and
median latency with dispersion over repeated fresh processes. Token and call
counts are reported as agent cost, not as a compiler property.

**Datasets.** The subject is a fixed model from the ONNX Model Zoo, the same
one the rest of the paper measures, so no arm gains an input advantage. This is
deliberately **not** an MLPerf evaluation: MLPerf Inference is the recognised
suite for end-to-end inference benchmarking, it presupposes a deployment stack
and accuracy protocol that this study does not implement, and claiming it
without running it would be the kind of overstatement the rest of this paper
avoids. The limitation is stated in the paper rather than left to the reader.

**Tasks.** Frozen in `tasks.json` before the pilot. Each task states the desired
behaviour in prose only; no task names a file, a symbol, or a line. A control
task asks for a change that requires no compiler reasoning, so a floor effect in
both arms is distinguishable from a task that is simply too hard.

**Agent.** A local model served by ollama, pinned by tag *and* digest, with
`temperature = 0` and a fixed `seed`. Same prompt template, same tool set, same
step cap in both arms. The harness records the model digest with every run.

**Outcome.** A run succeeds if the change is accepted by the compiler's own
checker in arm A, or compiles and passes the same oracle in arm B. Reported per
run: success, steps used, wall-clock seconds, rebuild cycles, model calls, and
whether a failed attempt left the working tree modified.

**Analysis.** Paired by task over N runs per arm. If the arms do not separate,
that is the result and is reported as such; no task is dropped after the fact.

## Threats to validity, stated in advance

1. **Model capability.** A 9B local model may fail both arms, which would show
   that the tasks are out of reach rather than that the mechanisms differ. The
   pilot exists to detect exactly this, and a task where both arms fail is
   reported as uninformative rather than as a tie.
2. **Confounds.** The arms differ in language and codebase as well as in the
   mechanism. The control task bounds this: if the control is equally easy in
   both arms, the difference on the real tasks is not merely tooling.
3. **Stochasticity.** Runs are pinned by model digest, seed, and temperature,
   and every transcript is retained so a reviewer can inspect a run.
4. **Harness authorship.** The harness, the prompts, and the tasks are written
   by the same authors as the system under test. The prompts and the task list
   are frozen in this directory before the runs, and the raw transcripts are
   kept, so the claim can be checked rather than trusted.

## Files

- `tasks.json` — frozen task specifications.
- `agent_loop.py` — one agent session: tool-calling loop with a step cap,
  writing a JSONL transcript.
- `run_campaign.py` — the task x arm x run matrix.
- `runs/` — transcripts and per-run records (created by the harness).

## Harness

`agent_loop.py` runs one session: a fixed prompt template, five tools
(`read_file`, `write_file`, `list_dir`, `run_shell`, `finish`), and a step cap
taken from `tasks.json`. Each run works in its own scratch copy, and every model
call, tool call, and tool result is appended to a JSONL transcript that is kept
beside the run record.

A run's `finished` flag is the model's own claim and is never the outcome.
Success is decided afterwards by the task's `success_<arm>` predicate evaluated
against the tree, so a model that declares victory without making the change
scores as a failure. The harness was smoke-tested on a throwaway constant-change
task before any study run: six steps, 363 tokens, 54 seconds, the change landed,
and the predicate confirmed it independently of the model's summary.

## Status: harness built and verified, task set not yet valid

The harness is finished and smoke-tested. The three-arm task set is not, and
running it now would produce a number that means nothing. Two blockers were
found by trying, and both are recorded rather than papered over.

**A control task must be comparable across the three systems, and it is not.**
The three arms expose different extension surfaces: Joggle takes a typed
function in a loaded module, TVM takes a Python or C++ extension against its
runtime, ONNX-MLIR takes an out-of-tree dialect and pass. A change that is
trivial in one idiom, such as adding a module definition, has no counterpart in
the other two, so a control built that way measures the idiom rather than the
mechanism. The control exists to bound tooling confounds, and no candidate
found so far does that.

**Reverting a historical commit to obtain a known-solvable task does not apply
cleanly.** The obvious route to a task that is provably solvable is to take a
real commit, revert it in a scratch tree, and ask the agent to restore the
behaviour, with the repository's own tests as the oracle. The tree has moved on:
`modules/opt` was split into fragments after the change in question, later work
rewrote the function it touched, and the helper it added no longer exists.
Reverting the file wholesale fails with duplicate definitions, and reverting the
change in place is no longer a mechanical operation.

The study therefore stays out of the manuscript. What is already established and
worth keeping: the harness runs and its success predicate is evaluated against
the tree rather than taken from the model's own summary; ollama tool calling
returns structured calls and is deterministic under a fixed seed; the Joggle CLI
is fully scriptable for an agent; and this codebase contains a genuine
discriminating check for such an agent, since its own test suite requires that a
user-defined `+` overload is *not* folded by the identity-folding pass. A task
built on that trap would separate an agent that reasons about the
representation from one that pattern-matches.
