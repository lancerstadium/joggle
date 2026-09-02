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
./build/residual_oracle --conditioned
./build/residual_oracle --exploration
./build/residual_oracle --materializer
./build/residual_oracle --mtbdd
./build/residual_oracle --transitions
```

Current coverage:

- all 32 F1-0 runtime-fact points;
- all 36 raw whole-graph assignments per point (1,152 evaluations total);
- structural, capability, and typed scratch-resource legality;
- exact latency/energy and resident-transition costs;
- deterministic canonical tie-breaking;
- internal invariants for legal-set size and optimum membership.
- exact-subset static/top-K portfolio baselines with explicit fallback;
- an exact minimum-cube conditioned-variant baseline;
- an optimal-variable-order reduced multi-terminal BDD baseline (the binary
  specialization of the preregistered MDD baseline);
- an Astra-style exhaustive online-exploration baseline with explicit trials;
- an independently encoded no-trial reference materializer that matches the
  oracle at all 32 points with declared bounds of 26 abstract steps and 5
  arena bytes.

The F1 transition harness also exhausts all 992 non-identity fact transitions,
checks the minimum staged atom set, injects interruption at every staged write,
and verifies atomic generation publication.

Not implemented yet: a separate FCC encoding, AND–OR memo extraction, F2/F3,
generated D3 scale sweeps, wall-clock instrumentation, or any real-target
adapter. The current materializer is a contract smoke test and is already
dominated by established encodings on F1-0; it is not a candidate contribution.
