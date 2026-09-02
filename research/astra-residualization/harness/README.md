# Residual-optimization diagnostic harness

This standalone C++20 program implements the independent exhaustive oracle required by the D1–D3 preregistration. It is intentionally not linked to Joggle: research validity should not depend on the candidate compiler implementation.

Build and test:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Emit the full F1-0 oracle table:

```sh
./build/residual_oracle > f1-oracle.csv
```

Inspect the first exact static/top-K baselines and the bounded reference
materializer:

```sh
./build/residual_oracle --baselines
./build/residual_oracle --materializer
```

Current coverage:

- all 32 F1-0 runtime-fact points;
- all 36 raw whole-graph assignments per point (1,152 evaluations total);
- structural, capability, and typed scratch-resource legality;
- exact latency/energy and resident-transition costs;
- deterministic canonical tie-breaking;
- internal invariants for legal-set size and optimum membership.
- exact-subset static/top-K portfolio baselines with explicit fallback;
- an independently encoded no-trial reference materializer that matches the
  oracle at all 32 points with declared bounds of 26 abstract steps and 5
  arena bytes.

Not implemented yet: a serialized residual artifact, conditioned-variant and
shared-plan baselines, D2 transitions, generated scale sweeps, or any
real-target adapter. The current materializer is a contract smoke test, not a
candidate contribution, and must not be used to claim D1 success.
