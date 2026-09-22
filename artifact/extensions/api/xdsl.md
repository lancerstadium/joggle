# xDSL analysis extension interface

Edit the supplied Python implementation. Its public entry point is:

```python
from xdsl.dialects.builtin import ArrayAttr, DictionaryAttr, IntegerAttr, ModuleOp

def analyze(module: ModuleOp) -> dict:
    return {}
```

The test driver parses a builtin module with a `study.request` dictionary attribute.
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
