# Engineering roadmap

This roadmap tracks project work, not manuscript claims. A capability moves
into the user guide only after its public interface, regression test, failure
behavior, and documentation agree.

## Current priorities

### 1. Stabilize the `mod` surface

- use `mod` as the only language keyword;
- reject retired spellings instead of maintaining forward-compatibility paths;
- keep package discovery deterministic across source and native packages;
- make dependency and collision diagnostics identify the responsible `mod`;
- keep examples, CLI help, tests, and documentation on one vocabulary.

Completion gate: a repository-wide syntax audit, parser rejection tests, and
the install-consumer test all pass without compatibility aliases.

### 2. Keep reactive execution precise

- maintain exact dependencies for functions, collections, operations, values,
  packages, and intrinsics;
- make invalidation reasons visible through the public report structures;
- reuse query and stage results only when every observed dependency is valid;
- propagate package and structure mutations conservatively;
- retain full verification for edits outside the supported local-update set.

Completion gate: repeated edits cover cache hits, exact misses, upstream
propagation, rollback, deletion, and handle invalidation in native tests.

### 3. Reduce evaluator overhead

- keep execution plans immutable and revision keyed;
- reuse register windows and predecoded call information;
- avoid graph-sized scans on local query and stage updates;
- measure allocations and peak memory before adding a new cache;
- remove instrumentation from release builds unless explicitly enabled.

Completion gate: correctness tests pass with instrumentation both enabled and
disabled, and a rejected optimization leaves no duplicate execution path.

### 4. Preserve explicit artifact boundaries

- conversion, optimization, planning, and emission remain explicit commands;
- emitters do not hide semantic rewrites or storage planning;
- generated C remains deterministic and warning-clean;
- public headers, structured API reports, and definitions share one ABI model;
- VM and C paths validate the same IR contracts independently.

Completion gate: generated artifacts compile under strict C11 warnings and
match checked references for the maintained operator and application suites.

### 5. Simplify the repository

- one root README for project entry and design narrative;
- one documentation tree for guides, reference, and internals;
- no manuscript, paper experiment, or promoted-result directory in the project;
- no tracked compiler or documentation build output;
- every guide example maps to a named test;
- generated files stay under ignored `build*` directories.

Completion gate: layout, documentation-graph, clean-build, install-consumer,
and GitHub Pages checks pass from a fresh clone.

## Test organization

The maintained labels are:

| Label | Responsibility |
| --- | --- |
| `unit` | native language, IR, evaluator, and codec contracts |
| `cli` | command parsing, diagnostics, streams, and reports |
| `analysis` | checked read-only analyses and policies |
| `extension` | out-of-tree `mod` packages |
| `c` | C emission, compilation, execution, and ABI checks |
| `example` | opt-in generated applications |
| `model` | pinned external model gates |
| `install` | exported package and downstream consumer |
| `lint` | documentation and repository layout |

New tests should extend an existing responsibility group. A new label is
justified only when it enables a materially different development loop.

## Documentation release gate

Before merging a public-interface change:

1. update the relevant guide or reference page;
2. update its named test rather than adding a tutorial-only implementation;
3. run `python3 test/docs.py .`;
4. build the Jekyll site through the Pages workflow;
5. verify internal links and the deployed landing page.

## Deferred work

The following ideas require separate designs and are not implied by the current
implementation:

- incremental native-object compilation;
- a general JIT backend;
- unrestricted incremental structural rewrites;
- automatic schedule search;
- distributed or persistent build caching;
- a stable 1.0 compatibility policy.

They should not add compatibility layers or public terminology before an
implementation proposal and a bounded test plan exist.
