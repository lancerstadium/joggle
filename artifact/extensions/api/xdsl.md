# xDSL analysis extension interface

Edit the supplied Python implementation. Its public entry point is:

```python
from xdsl.dialects.builtin import ArrayAttr, DictionaryAttr, IntegerAttr, ModuleOp

def analyze(module: ModuleOp) -> dict:
    return {}
```

For attribute-request tasks, the driver parses a builtin module with a
`study.request` dictionary attribute.
Read that native IR attribute with:

```python
request = module.attributes["study.request"]
assert isinstance(request, DictionaryAttr)
items = request.data["items"]
assert isinstance(items, ArrayAttr)
count = len(items)
first = items.data[0].value.data
```

Arrays are iterable; their `data` field contains the attribute sequence.
For `IntegerAttr`, `value.data` is the integer value. Integer attributes in
this interface use `i64`. Standard Python containers, control flow, and
arithmetic are available. Return a JSON-compatible dictionary:

```python
return {"count": count, "values": [first]}
```

The driver loads the extension with the pinned xDSL interpreter, calls
`analyze`, and compares its JSON result to the task's expected result. Return
semantic rejection as the error dictionary specified by the task, rather than
aborting the process. Do not change the driver or fixtures.

## Native graph analysis

For `ana-fusion-match`, the registered `func` dialect represents real typed
calls, not a `study.request` dictionary. The `FuncOp` named `subject` has a
`layout` string attribute (`NCHW` or `NHWC`). Callee symbols include `conv2d`,
`bias_add`, and `relu`; other calls may also occur.

```python
from xdsl.dialects.func import CallOp, FuncOp
from xdsl.dialects.builtin import TensorType
subject = next(op for op in module.ops
               if isinstance(op, FuncOp) and op.sym_name.data == "subject")
calls = [op for op in subject.body.block.ops if isinstance(op, CallOp)]
callee = calls[0].callee.root_reference.data
producer = calls[0].arguments[0].owner
single_use = calls[0].res[0].has_one_use()
shape = calls[0].res[0].type.get_shape()
```

Check `isinstance(producer, CallOp)` before inspecting a defining call; block
arguments have a block owner. Tensor types expose `get_shape()` as an integer
tuple. Report each match as indices into the subject's call sequence,
excluding its return. Uses include every actual operand occurrence and returns.
