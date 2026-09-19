# Making preparation cost proportional to the work, not to the graph

## The measurement

Preparation is about two milliseconds per operation across the campaign: 1,108
operations take 1.2 s and 17,467 take 43.6 s. SSD-MobileNetV1 has 19,432 operations,
so that law predicts under a minute, and it takes tens of minutes instead. The
excess is in `opt.expose`'s fallback path, which expands calls one at a time:
`Env::expand` is invoked 7,278 times on this graph at a measured 435 ms each.

435 ms for inlining one call into a 19,432-operation module is not the inlining.
It is the fixed work each invocation does around it, and that work is sized by the
whole store rather than by the call:

| Site in `src/env.cpp` | Per invocation |
| --- | --- |
| 814 `detail::Store backup = mod.impl_->store` | a full copy of the store |
| 865 `collect_locals` | a recursive walk of the callee's local closure |
| 931 `mod.clone(...)` per dependency | clone each private helper |
| 979, 983, 1008, 1036 `mod.ops()` | four allocations and walks of every op |
| 1058 `detail::Dom dom(mod.impl_->store)` | three vectors sized by the store |

Everything above is per *call*, so the fallback makes it per call 7,278 times.

## What the mature systems do instead

MLIR's dialect conversion has the same shape as `Env::expand`: a rewriter records
changes, a pattern that fails causes a rollback, and success applies the changes.
Two things about it are worth copying.

**Rollback is an undo log, not a copy.** `ConversionPatternRewriterImpl` keeps a
`std::vector` of rewrites and `undoRewrites` calls `rollback()` on each in reverse.
The cost of a failed pattern is proportional to what the pattern changed, not to the
size of the module.

**They are removing rollback from the hot path entirely.** LLVM commit 8bc0d4d adds
`allowPatternRollback` to `ConversionConfig`, and when it is false a rollback is a
fatal error; a failed conversion leaves the IR as a mix of original and rewritten
operations rather than restoring it. The commit says it prepares for a one-shot
conversion driver that "will remove the ability to roll back IR modifications". The
reason is the same one this document is about: transactional rewriting costs the
whole IR on every attempt.

The greedy pattern rewriter next to it takes the other half of the answer: it holds
a worklist of operations to revisit, pushes the users of a rewritten operation back
onto that worklist, and stops when the worklist is empty. It does not re-walk the
module to find out what changed.

## The refactor, in the order that pays

**1. Record rewrites instead of copying the store (env.cpp:814).**
This is the largest single item and the most mechanical. Every mutation
`Env::expand` performs goes through a small number of `Mod` methods, so each can
append an undo entry: erase this op, restore this value's type, retarget this call.
`rollback()` then replays the log in reverse. A failed attempt stops costing a store
copy, and the dominance check below can be scoped to the new operations.

*Effect*: removes one full store copy and one full store restore per invocation.

**2. Ask the expander which operations it created (env.cpp:979, 983, 1008, 1036).**
`Mod::expand` knows exactly which operations it inserts, and `Env::expand`
immediately re-derives that by diffing against a saved copy of every op. Return the
new operations instead, and the retarget walk touches only them.

*Effect*: four whole-module walks per invocation become walks over one body.

**3. Cache the local closure per implementation (env.cpp:865, 931).**
The closure of a callee is a property of the callee and the revision, not of the
call. In the fallback the same helpers are collected and cloned 7,278 times. Key a
cache on (implementation, revision) and materialise each closure once per batch.

*Effect*: removes the recursive walk and the clones from all but the first call for
each distinct callee. Note that grouping the fallback by callee was tried and did
not help, which says the callees are *not* concentrated -- so this alone may buy
little, and it should be measured before it is trusted.

**4. Scope the dominance check (env.cpp:1058).**
`Dom` allocates three vectors over the whole store and the check walks every op, to
verify dominance for the handful of operations just inserted. Either build it once
per batch and extend it, or check only the inserted operations against their
definitions directly.

*Effect*: removes three store-sized allocations and one whole-module walk per
invocation.

**5. Stop making the batch atomic (expand_with in modules/opt/lib/lower.jog).**
The batch fails late and rolls back the thousands of expansions that had already
succeeded, and the fallback then repeats all of them. This is what MLIR's
`allowPatternRollback = false` changes, and the caller here is already written for
it: it retries call by call. Let the batch keep its successful prefix and report
where it stopped.

*Effect*: turns roughly two passes over the batch into one, once items 1-4 have made
a pass cheap. On its own it does not help, which is why it is last.

## Scale, and what this does not fix

Items 1, 2 and 4 are the ones the profile points at: they are what makes 435 ms out
of an operation that should take microseconds, and each is a bounded change to one
function plus its callees. Item 5 changes a transaction's semantics and needs the
callers and the tests that depend on atomicity to be re-examined -- `test/network.cpp`
asserts that a failed expand leaves the module untouched, and it caught exactly this
class of change once already.

None of this makes preparation linear in graph size; it removes work that is
proportional to the graph from a path that runs per call. The remaining cost would
be the interpreted preparation itself, which is the prototype's design and is
reported as such in the paper. Even a large constant-factor win leaves the largest
graphs slow, and the honest claim afterwards would still be about the models that
were measured.

## How to check it

The measurement that identified the cost is cheap and should be the first thing run:
make the fallback stop after twenty calls and report how long that took, which gives
milliseconds per expansion without waiting for a full preparation. UltraFace at
17,467 operations and 43.6 s is a usable intermediate target, and mnist-8 at 1,108
operations and 1.2 s is the floor a fix must not regress.
