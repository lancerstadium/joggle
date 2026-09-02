# Interfaces

An interface is a versioned semantic contract over one subject kind. It lets a
pass ask what a type, attribute, or operation provides without naming its
concrete declaration.

```joggle
interface numeric_format: type {
  storage_bits() -> i64;
  is_signed() -> bool;
}

type integer(width: i64, signed: bool = false) : numeric_format;
```

Marker interfaces omit the body. Imported interfaces use a qualified name:

```joggle
import arith@1;
op relu<T: type>(input: T) -> T : arith.elementwise;
```

The linker checks every conformance against the complete Module closure and the
interface subject kind. The interface, conforming declaration, and method all
retain version and digest identity; equal spelling in unrelated Modules does
not imply compatibility.

## Typed C++ behavior

The C++ API uses reflected declarations plus generic codecs:

```cpp
auto arith = compiler.module("arith");
auto integer = arith->type("integer");

compiler.bind(*integer, "storage_bits",
  [](const joggle::Type& type) {
    return type.get<std::int64_t>("width");
  });

auto i8 = compiler.make(*integer, 8, false);
auto bits = compiler.call<std::int64_t>(*i8, "storage_bits");
```

`Compiler::bind` rejects a declaration that does not conform to the method's
interface. For an ordinary lambda it infers the result and arguments, then
checks them against the method signature in the Module before storing anything.
Calls validate argument kinds, dispatch by exact declaration/method identities,
and validate the result and Module closure. A short method name is resolved only
across interfaces implemented by the subject. A real collision can use an
explicit method declaration handle.
