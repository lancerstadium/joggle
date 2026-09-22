from xdsl.dialects.builtin import ArrayAttr, DictionaryAttr, IntegerAttr, ModuleOp


def analyze(module: ModuleOp) -> dict:
    request = module.attributes["study.request"]
    assert isinstance(request, DictionaryAttr)

    def read_shape(key: str) -> list[int]:
        value = request.data[key]
        assert isinstance(value, ArrayAttr)
        assert all(isinstance(extent, IntegerAttr) for extent in value)
        return [extent.value.data for extent in value]

    lhs, rhs = read_shape("lhs"), read_shape("rhs")
    if any(extent < 0 for shape in (lhs, rhs) for extent in shape):
        return {"error": "invalid-extent", "phase": "build"}
    rank = max(len(lhs), len(rhs))
    result = [1] * rank
    for offset in range(rank):
        a = lhs[-offset - 1] if offset < len(lhs) else 1
        b = rhs[-offset - 1] if offset < len(rhs) else 1
        if a != b and a != 1 and b != 1:
            return {"legal": False, "conflict_axis_from_end": offset}
        result[-offset - 1] = b if a == 1 else a
    return {"legal": True, "shape": result}
