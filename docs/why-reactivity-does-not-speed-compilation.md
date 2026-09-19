# Why cheap revision has not made compilation faster

## The question

If compiler decisions are typed functions in a shared IR, and a revision can be
derived, verified and rolled back without rebuilding the compiler, why is the
resulting compiler 22.5 times slower to compile a model than TVM or ONNX-MLIR on the
same host (101.5 s against 4.52 s and 4.51 s, one model, five runs each)?

## Four reasons, in order of how much each explains

**1. The innovation was never aimed at compilation throughput.**

What was measured and claimed is edit latency and derivation cost: deriving a
locality recipe costs 0.04 s, deriving the planner 2.48 s, and one revision from edit
to revised plan costs 2.53 s against 8.03 s for a translation unit and a relink on
TVM's route, which follows a 514 s clean build. Nothing in that set is a claim about
how fast the first compilation is, and the paper reports the 22.5x the other way
without hedging it.

So part of the answer is that these are different quantities, and the comparison
that makes Joggle look slow is not the one its mechanism targets.

**2. Dependency-sensitive invalidation, the thing that would make reactivity pay,
is not implemented.**

This is the substantive answer. The compiler has a query cache, and every mutation
clears it outright:

    src/ir.cpp:287    store.queries.clear();
    src/ir.cpp:548    store.queries.clear();
    src/env.cpp:977   mod.impl_->store.queries.clear();
    src/env.cpp:1057  mod.impl_->store.queries.clear();

There is no tracking of which cached answer depended on which definition, so there
is nothing to invalidate selectively. The paper says the same in words: cached
name-resolved queries are scoped by environment identity, epoch, subject revision,
function and arguments, and "do not track individual dependencies".

The consequence is that revising one decision invalidates at subject-revision
granularity, which is to say everything. Reactive recompilation without a dependency
graph is full recompilation, and the cache stops being a reuse mechanism and becomes
a within-run memo table.

**3. The features that make a revision trustworthy are charged per attempt and sized
by the graph.**

The contribution is that a derived procedure is verified and can be rolled back, so
a revision is reproducible rather than a hopeful edit. Each of those is correct, and
each is paid for on every attempt:

- rollback copies the entire store (`src/env.cpp:814`), so a failed attempt costs
  what a successful one costs;
- verification is module-wide;
- the cache key includes the subject revision, so any edit moves the key.

A conventional compiler mutates in place and does not snapshot, verify or key
anything; it is faster precisely because it gives up the guarantees Joggle is
selling. The comparison is not like for like, and the paper's own framing should say
so more loudly than it does.

**4. Preparation is interpreted, which is a constant factor on top of all of it.**

The profile puts essentially all of the time in the evaluator running module code,
with attribute and type handling beneath it. That is the prototype's design and the
paper reports it as a representational overhead.

## What the numbers say about where the malleability layer sits

A single-call expansion on SSD-MobileNetV1's 19,432-operation graph takes 435 ms.
Inlining one call is microseconds of actual work. The rest is the bookkeeping above:
a store copy, a closure walk, a clone per private helper, four whole-module walks,
and a dominance structure of three store-sized vectors. The malleability layer is
therefore not merely failing to accelerate preparation; on this graph it dominates
it.

## The honest framing

The architecture makes dependency-sensitive revision *possible* -- every decision is
a value in one IR, derivations are structurally addressed, and a revision carries its
own verification -- but the implementation does not yet *realise* it, because nothing
records what depends on what. That gap is the difference between "a revision can be
expressed and validated cheaply" and "a revision is cheap", and the paper currently
claims only the first while the second is what a reader will hear.

Closing it is the highest-value thing left to build, and it is a different project
from making any one model compile: it means recording, for each cached answer, the
definitions it read, and invalidating by reachability from the edit rather than by
revision equality. The refactor in `preparation-cost-refactor.md` removes the
per-attempt costs that make the current situation worse; this would be the change
that makes reactivity pay.
