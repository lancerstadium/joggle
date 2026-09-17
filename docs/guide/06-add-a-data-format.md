# Add a data format and primitive

Build the optional saturating-integer module and run its type-directed selector:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJOGGLE_BUILD_SAT=ON
cmake --build build
./build/joggle run sat.select test/data/sat.jog -M build/modules
```

The module declares its type constructor, symbolic algebra, and structural
format predicate as ordinary functions:

```jog
fn sat<W: int>() -> Ty;
fn +<W: int>(a: sat<W>, b: sat<W>) -> sat<W>;
fn add<W: int>(a: sat<W>, b: sat<W>) -> sat<W>;
fn supports(type: Ty) -> bool;
```

After verification, embedding code can inspect the actual overload selected
for any call:

```cpp
joggle::Fn target = env.resolve(mod, addition);
if (!target)
  return mod.print_diags(stderr);
```

The target bridge is separate from both the format and emitter:

```sh
./build/joggle run sat.c.prepare test/data/sat_c.jog \
  -M build/modules > prepared-sat.jog
./build/joggle emit c.source prepared-sat.jog \
  -M build/modules > prepared-sat.c
./build/joggle run sat.vm.prepare test/data/sat_vm.jog \
  -M build/modules > prepared-sat-vm.jog
```

`sat.materialize` recursively replaces concrete format types and clones one
generic helper per encountered width. `sat.c` supplies the ordered C storage
map and leaves ordinary local calls for `c.source`; `sat.vm` instead maps the
same format to `i64` before normal VM preparation. Repeating either preparation
produces identical IR. Another format can use the same target boundaries
without changing `c` or `vm`.

The predicate uses ordinary `Ty` reflection rather than a native string parser.
The `sat<8>` addition becomes `sat.add(a, b)` while the `i32` addition remains
an ordinary `a + b`. `Env::call` invokes `sat.sim(8, 100, 100)` to obtain the
saturated result `127`, or `sat.emit(8)` to obtain a standalone SystemVerilog
implementation. `test/sat.cpp` exercises selection, idempotence, both
saturation limits, type rejection, and emitter structure.

