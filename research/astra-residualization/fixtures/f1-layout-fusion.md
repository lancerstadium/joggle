# F1-0 hand-checkable layout–fusion instance

> Status: frozen before oracle implementation
> Purpose: first D1 legality/performance oracle fixture; not a neural-network benchmark

## Semantic graph

```text
input ── stem(conv + optional relu) ── activation ──┐
input ── skip(identity or conversion) ──────────────┼─ add ── canonical NCHW output
```

Every atom below is assumed to have passed local semantic validation. The oracle tests whether atoms compose into one legal whole-graph realization.

## Runtime facts

The exhaustive domain is the Cartesian product of five binary facts: 32 fact points.

| Fact | Values | Meaning |
|---|---|---|
| `shape` | `small`, `large` | Selects the frozen per-atom cost column. |
| `tensor` | `off`, `on` | Whether atoms requiring the tensor execution resource are admissible. |
| `scratch` | `tight=4`, `wide=8` | Maximum simultaneous abstract scratch units. |
| `objective` | `latency`, `energy` | Chooses which exact integer cost is minimized. |
| `resident` | `nchw`, `nhwc` | Layout of the resident stem-side code/data generation; changing it adds a transition term. |

## Choice sites and atoms

Costs are `(latency_small, latency_large; energy_small, energy_large)`. Scratch is an abstract typed-resource demand.

| Site | Atom | Required fact | Output/input state | Scratch | Costs |
|---|---|---|---|---:|---|
| stem | `stem.nchw` | — | output NCHW, relu not fused | 1 | `(8,20; 10,24)` |
| stem | `stem.nhwc` | `tensor=on` | output NHWC, relu not fused | 1 | `(10,12; 8,13)` |
| stem | `stem.nhwc_relu` | `tensor=on` | output NHWC, relu fused | 4 | `(8,9; 7,10)` |
| activation | `act.nchw` | — | NCHW relu | 1 | `(3,5; 3,5)` |
| activation | `act.nhwc` | — | NHWC relu | 0 | `(2,3; 2,3)` |
| activation | `act.none` | — | no separate work | 0 | `(0,0; 0,0)` |
| skip | `skip.nchw` | — | output NCHW | 0 | `(1,2; 1,2)` |
| skip | `skip.nhwc` | — | convert to NHWC | 1 | `(3,4; 4,5)` |
| join | `join.nchw` | — | inputs NCHW, output NCHW | 1 | `(2,3; 2,3)` |
| join | `join.nhwc_store` | — | inputs NHWC, convert output to NCHW | 2 | `(4,5; 3,4)` |

There are `3 × 3 × 2 × 2 = 36` raw assignments per fact point and `1,152` assignment/fact pairs in the exhaustive domain.

## Forbidden conjunctions

1. `stem.nchw` with `act.nhwc` or `act.none`.
2. `stem.nhwc` with `act.nchw` or `act.none`.
3. `stem.nhwc_relu` with `act.nchw` or `act.nhwc`.
4. `join.nchw` with `stem.nhwc`, `stem.nhwc_relu`, or `skip.nhwc`.
5. `join.nhwc_store` with `stem.nchw` or `skip.nchw`.
6. Any atom requiring `tensor=on` is illegal when `tensor=off`.
7. Sum of selected atom scratch demands must not exceed the `scratch` limit.

These constraints intentionally leave three structurally legal paths before fact/resource filtering:

- `stem.nchw + act.nchw + skip.nchw + join.nchw`;
- `stem.nhwc + act.nhwc + skip.nhwc + join.nhwc_store`;
- `stem.nhwc_relu + act.none + skip.nhwc + join.nhwc_store`.

The unfused NHWC path consumes `1 + 0 + 1 + 2 = 4` units. The fused path consumes `4 + 0 + 1 + 2 = 7` and is therefore illegal under `scratch=tight`.

## Interaction and transition costs

- Selecting `stem.nhwc + act.nhwc + join.nhwc_store` adds `(latency 2, energy 1)` on `shape=small`, representing a small-graph backend boundary penalty; it adds zero on `shape=large`.
- If the selected stem layout differs from `resident`, add transition `(latency 3, energy 2)`.

No cost changes legality. Tie-breaking uses the lexicographically sorted atom-id vector.

## Expected oracle invariants

- Exactly 36 raw assignments are visited for every fact point.
- Exactly one legal realization exists whenever `tensor=off`.
- With `tensor=on,scratch=tight`, the NCHW and unfused NHWC paths are legal; the fused path is illegal.
- With `tensor=on,scratch=wide`, all three structural paths are legal.
- Every chosen optimum belongs to the independently enumerated legal set.
- Repeating the same fact point returns an identical canonical realization and cost.
