from xdsl.dialects.builtin import ArrayAttr, DictionaryAttr, IntegerAttr, ModuleOp, StringAttr


def analyze(module: ModuleOp) -> dict:
    request = module.attributes["study.request"]
    assert isinstance(request, DictionaryAttr)

    def ordered(key):
        array = request.data[key]
        assert isinstance(array, ArrayAttr)
        assert all(isinstance(value, StringAttr) for value in array)
        return sorted(value.data for value in array)

    target, bits, endian = (request.data[key] for key in ("target", "pointer_bits", "little_endian"))
    assert isinstance(target, StringAttr)
    assert isinstance(bits, IntegerAttr) and isinstance(endian, IntegerAttr)
    return {"target": target.data, "pointer_bits": bits.value.data,
            "little_endian": bool(endian.value.data), "features": ordered("features"),
            "intrinsics": ordered("intrinsics")}
