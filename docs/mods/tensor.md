# Tensor

`tensor@7` supplies the target-independent tensor value and five ordinary fns:

```joggle
type tensor(element: type, shape: list<int>);
fn tensor<E,S:list<int>>(
  shape: S,
  body: callable<@repeat(index, length(S)), [E]>
) -> tensor<E,S>;
fn map<E,S:list<int>,R>(
  input: tensor<E,S>, body: (E) -> R
) -> tensor<R,S>;
fn reduce<A,N:int>(
  extent: N, initial: A, body: (A, index) -> A
) -> A;
fn ([])<E,S:list<int>>(input: tensor<E,S>, indices: index...) -> E;
fn constant<T>(content: string) -> T;
```

Known-rank construction uses normal callback parameters:

```joggle
return t.tensor([3,2], (row, column) => input[column,row]);
```

Rank-polymorphic elementwise code consumes values instead of exposing an index
list:

```joggle
return t.map(input, (value) => max(value, zero(value)));
```

`reduce` expresses a pure indexed reduction; its callback returns the next
accumulator value and contains no implicit mutation. A construction callback
may use local scalar bindings and `return`, but tensor form does not mix an
imperative `for` into construction. `constant` refers to immutable bytes owned
by the enclosing `Mod`. Tensor does not define NN operators, layouts, buffers,
schedules, devices, or hardware resources.
