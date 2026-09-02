# F2-0 hand-checkable branch placement–schedule instance

> Status: frozen before oracle implementation
> Purpose: D1/D2 fixture for branch-local resource and event effects

## Semantic graph

```text
input ── left convolution/projection ──┐
                                      ├─ add ── output
input ── right skip/projection ────────┘
```

Each implementation atom is locally semantics-preserving. The whole-graph legality question is whether two branch atoms can be placed and scheduled under the current typed resources and event capability.

## Runtime facts

Six binary facts form 64 exhaustive states.

| Fact | Values | Meaning |
|---|---|---|
| `shape` | `small`, `large` | Selects frozen branch cost columns. |
| `tensor` | `off`, `on` | Capacity of typed resource `tensor` is 0 or 1. |
| `copy` | `off`, `on` | Capacity of typed resource `copy` is 0 or 1. |
| `vec_lanes` | `one`, `two` | Capacity of typed resource `vec` is 1 or 2. |
| `fine_events` | `off`, `on` | Whether pairwise branch completion events are supported. |
| `objective` | `latency`, `energy` | Selects the exact integer optimization objective. |

These are module-defined facts. The harness core must not interpret them as a fixed GPU/FPGA/RISC-V device struct.

## Choice sites

Costs are `(latency_small, latency_large; energy_small, energy_large)`.

| Site | Atom | Capability/resource | Costs |
|---|---|---|---|
| left | `left.vec` | `vec:1` | `(8,16; 6,12)` |
| left | `left.tensor` | `tensor:1`, requires `tensor=on` | `(7,8; 9,10)` |
| right | `right.vec` | `vec:1` | `(7,13; 5,9)` |
| right | `right.tensor` | `tensor:1`, requires `tensor=on` | `(5,7; 8,9)` |
| right | `right.copyvec` | `copy:1`, requires `copy=on` | `(9,10; 3,4)` |
| schedule | `schedule.serial_global` | sequential branches, global join | latency overhead 2, energy overhead 1 |
| schedule | `schedule.overlap_pair` | concurrent branches, requires `fine_events=on` | latency overhead 1, energy overhead 2 |

Every realization also contains the fixed atom `join.add`.

There are `2 × 3 × 2 = 12` raw assignments per fact point and `768` assignment/fact pairs.

## Legality

1. An atom requiring `tensor` or `copy` is illegal when the corresponding capacity is zero.
2. `schedule.serial_global` checks each branch independently; resources are not concurrent.
3. `schedule.overlap_pair` requires `fine_events=on` and the element-wise sum of concurrent branch resource use must not exceed capacity.
4. Therefore `left.tensor + right.tensor` can never overlap because `tensor` capacity is at most one.
5. `left.vec + right.vec` can overlap only when `vec_lanes=two`.
6. `join.add` executes only after the schedule's declared global or pairwise completion event.

## Cost

- Serial latency: `left_latency + right_latency + 2`.
- Serial energy: `left_energy + right_energy + 1`.
- Overlap latency: `max(left_latency, right_latency) + 1`.
- Overlap energy: `left_energy + right_energy + 2`.

The overlap schedule may reduce latency while slightly increasing energy. Local fastest branch choices are therefore not sufficient to determine the whole realization.

## Expected invariants

- Exactly 12 raw assignments are visited per fact point.
- `left.vec + right.vec + schedule.serial_global + join.add` is legal in all 64 states and is the explicit fallback.
- When `fine_events=off`, no overlap realization is legal.
- When `vec_lanes=one`, the two vector branches cannot overlap.
- The two tensor branches never overlap.
- Every oracle optimum belongs to the independently enumerated legal set and is deterministic under lexicographic tie-breaking.

## D2 expectation, not a pass assumption

All realizations retain `join.add`; many one-fact changes should alter only one branch or the schedule atom. This fixture should improve delta locality relative to F1. It still fails D2 if P95 staging remains above 70%, fewer than 90% of one-fact changes avoid full staging, or atomic failure injection fails.
