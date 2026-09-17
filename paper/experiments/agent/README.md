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

**Arms.**
- **A, malleable.** The agent works in a scratch copy of a Joggle module
  directory. It may read the module sources, edit `.jog` files, and run
  `joggle check`, `joggle query`, and `joggle emit` against the copy. The
  installed original is untouched, so a failed attempt cannot damage it.
- **B, maintained implementation.** The agent works in a scratch copy of the
  equivalent decision as it exists inside a separately maintained compiler. It
  may edit and rebuild. Rebuild cycles are counted.

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
