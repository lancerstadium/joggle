# Tensor

`tensor@8.0` owns target-independent tensor value semantics and the two
currently implemented tensor passes.

## Value API

```joggle
pub type tensor(element: type, shape: list<int>);

pub fn tensor<E, S: list<int>>(
  shape: S,
  body: callable<@repeat(index, length(S)), [E]>
) -> tensor<E, S>;

pub fn map<E, S: list<int>, R>(
  input: tensor<E, S>,
  body: (E) -> R
) -> tensor<R, S>;

pub fn reduce<A, N: int>(
  extent: N,
  initial: A,
  body: (A, index) -> A
) -> A;

pub fn ([])<E, S: list<int>>(
  input: tensor<E, S>,
  indices: index...
) -> E;

fn empty<E, S: list<int>>(shape: S) -> tensor<E, S>;

fn set<E, S: list<int>>(
  input: tensor<E, S>,
  value: E,
  indices: index...
) -> tensor<E, S>;

pub fn constant<T>(content: string) -> T;
```

`tensor`, `map`, `reduce`, `[]`, and `constant` are public. `empty` and `set`
are private implementation details of loop expansion; target-independent
tools inspect their exact declaration identity in produced Fns instead of
importing or spelling them in source. `set` returns a new tensor value;
it is not an effectful physical store. `constant` refers to immutable bytes
owned by a Mod.

Tensor defines no NN operator, layout, device, instruction, capacity, or
emitter.

## Compiler-time API

```joggle
pub fn fuse(input: fn) -> fn;
pub fn loops(input: fn) -> fn;
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
