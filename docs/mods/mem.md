# Logical memory

`mem@1` is the target-independent boundary between pure tensor values and
explicit writes. It imports `tensor@7` for immutable values and `arith@1` for
the generic loop arithmetic emitted by realization. It defines two
capabilities rather than a machine buffer class:

```joggle
type view(element: type, shape: list<int>);
type sink(element: type, shape: list<int>);

fn view<E,S:list<int>>(input: t.tensor<E,S>) -> view<E,S>;
fn ([])<E,S:list<int>>(input: view<E,S>, indices: index...) -> E;
fn alloc<E,S:list<int>>(shape: S)
  -> (sink<E,S>, effect<sink<E,S>>);
fn store<E,S:list<int>>(
  output: sink<E,S>, state: effect<sink<E,S>>,
  value: E, indices: index...
) -> effect<sink<E,S>>;
fn seal<E,S:list<int>>(
  output: sink<E,S>, state: effect<sink<E,S>>
) -> t.tensor<E,S>;
fn realize(input: fn) -> fn;
```

A `view` is readable and immutable. A `sink` is writable but cannot be read;
its affine `effect<sink<...>>` value orders writes and prevents one state from
being consumed twice on a control-flow path. `seal` consumes the final state
and exposes the completed value as an immutable tensor. The separation avoids
silently inventing alias or read-after-write rules.

The module intentionally contains no byte address, stride, layout, address
space, capacity, cache, bank, device, DMA, or instruction concept. Later
storage and target passes refine these logical capabilities through ordinary
types and fns in their own modules. `mem` is also not a kernel language: its
calls occur in the same `Fn` as scalar calls and ordinary loops.

`@mem.realize(input)` is the first destination-directed refinement pass. It
first expands ordinary bodyful fns, then handles the small tensor basis rather
than NN operator names. Indexed construction and `map` become loop nests;
`reduce` becomes an SSA accumulator loop; tensor reads become `view` indexing;
and the result is written through one `sink` token. Recursive coordinate
sampling fuses nested maps into that same write nest. MatMul and Relu therefore
use the same mechanism.

The current pass deliberately accepts a normalized, single-block fn with one
concrete-shape tensor result. This is an implementation frontier, not a second
IR contract. Whole-Mod destination passing, multiple results, shared tensor
values, constants, and view algebra are subsequent pass work.
