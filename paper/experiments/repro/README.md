# A reduction of SSD-MobileNetV1's preparation blocker

`cast-multi-use.jog` fails preparation in about a tenth of a second. It is the
smallest shape of the failure that strands SSD-MobileNetV1 after its passes
complete, which takes 35 s on the full graph.

## What fails and what does not

| variant | result |
| --- | --- |
| `return tensor.cast(x)` | prepares |
| `let f = tensor.cast(x)  return f` | prepares |
| `let f = tensor.cast(x)  return f + f` | fails |

## The root cause

A generic parameter that appears **only in the return type** is not inferred at a
call whose result is bound to a variable. `tensor.cast` has exactly that shape:

    fn cast<A: Ty, B: Ty, S: list<int>>(x: tensor<A, S>) -> tensor<B, S>

`B` is the target element type and appears nowhere in the parameters, so the call
has to get it from the expected type. Returning the call directly supplies that
expectation; binding it to a variable does not, and the parameter is left
unsubstituted. Inlining then copies the body with `B` still in it, the conversion
`B(x[i])` becomes a call whose callee is the type parameter, and preparation stops
on it.

The evidence is a probe on the substitution that runs while a callee's body is
copied into the call site:

    callee='B'  valid=1  bindings=2  ->  '_'
    callee='E'  valid=1  bindings=2  ->  'f32'

The same substitution resolves another function's parameter to `f32` and leaves
`B` unknown.

Annotating the binding explicitly does not help, so the expected type is not
reaching the call's generic inference through the annotation either. That is where
a fix belongs.
