from xdsl.dialects.builtin import ArrayAttr, DictionaryAttr, FloatAttr, IntegerAttr, ModuleOp, StringAttr


def analyze(module: ModuleOp) -> dict:
    request = module.attributes["study.request"]
    assert isinstance(request, DictionaryAttr)
    graph = request.data.get("graph")
    steps = graph.data if isinstance(graph, ArrayAttr) else (request,)

    def number(attr):
        assert isinstance(attr, (FloatAttr, IntegerAttr))
        return attr.value.data

    def interval(attr, fallback):
        if attr is None:
            return fallback
        assert isinstance(attr, ArrayAttr)
        return tuple(map(number, attr))

    result = (0.0, 0.0)
    for step in steps:
        assert isinstance(step, DictionaryAttr)
        op = step.data["op"]
        assert isinstance(op, StringAttr)
        a, b = interval(step.data.get("lhs"), interval(step.data.get("value"), result))
        c, d = interval(step.data.get("rhs"), (0.0, 0.0))
        if a > b or c > d:
            return {"error": "invalid-interval", "phase": "build"}
        if op.data == "add":
            result = (a + c, b + d)
        elif op.data == "mul":
            products = (a*c, a*d, b*c, b*d)
            result = (min(products), max(products))
        elif op.data == "relu":
            result = (max(a, 0.0), max(b, 0.0))
        elif op.data == "clamp":
            lower, upper = number(step.data["lower"]), number(step.data["upper"])
            if lower > upper:
                return {"error": "invalid-interval", "phase": "build"}
            result = tuple(min(max(value, lower), upper) for value in (a, b))
        else:
            return {"error": "unsupported-op", "phase": "build"}
    return {"interval": list(result)}
