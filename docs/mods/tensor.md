# Tensor

`tensor@7.1` owns target-independent tensor value semantics and the two
currently implemented tensor passes.

## Value API

```joggle
type tensor(element: type, shape: list<int>);

fn tensor<E, S: list<int>>(
  shape: S,
  body: callable<@repeat(index, length(S)), [E]>
) -> tensor<E, S>;

fn map<E, S: list<int>, R>(
  input: tensor<E, S>,
  body: (E) -> R
) -> tensor<R, S>;

fn reduce<A, N: int>(
  extent: N,
  initial: A,
  body: (A, index) -> A
) -> A;

fn ([])<E, S: list<int>>(
  input: tensor<E, S>,
  indices: index...
) -> E;

fn empty<E, S: list<int>>(shape: S) -> tensor<E, S>;

fn set<E, S: list<int>>(
  input: tensor<E, S>,
  value: E,
  indices: index...
) -> tensor<E, S>;

fn constant<T>(content: string) -> T;
```

`tensor`, `map`, `reduce`, and `[]` are the pure semantic basis.
`empty` and `set` are the loop-form basis. `set` returns a new tensor value;
it is not an effectful physical store. `constant` refers to immutable bytes
owned by a Mod.

Tensor defines no NN operator, layout, device, instruction, capacity, or
emitter.

## Compiler-time API

```joggle
fn fuse(input: fn) -> fn;
fn loops(input: fn) -> fn;
```

`fuse` expands bodyful calls and composes access paths into one output tensor
construction. `loops` accepts that fused construction and emits CFG loops with
`empty`, `[]`, and `set`. Calls are explicit:

```joggle
fused = @t.fuse(model);
looped = @t.loops(fused);
```

Neither pass invokes the other. Their exact input and output invariants are in
[Tensor compilation pipeline](../pipeline.md).
