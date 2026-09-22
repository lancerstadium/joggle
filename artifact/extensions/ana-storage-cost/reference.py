from xdsl.dialects.builtin import ArrayAttr, DictionaryAttr, IntegerAttr, ModuleOp


def analyze(module: ModuleOp) -> dict:
    request = module.attributes["study.request"]
    assert isinstance(request, DictionaryAttr)
    shape = request.data["shape"]
    width = request.data["element_bits"]
    assert isinstance(shape, ArrayAttr) and isinstance(width, IntegerAttr)
    elements = 1
    for value in shape:
        assert isinstance(value, IntegerAttr)
        extent = value.value.data
        if extent < 0:
            return {"error": "invalid-extent", "phase": "build"}
        elements *= extent
    return {"bytes": (elements * width.value.data + 7) // 8}
