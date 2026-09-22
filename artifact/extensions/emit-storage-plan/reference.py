from xdsl.dialects.builtin import ArrayAttr, DictionaryAttr, IntegerAttr, ModuleOp, StringAttr


def analyze(module: ModuleOp) -> dict:
    request = module.attributes["study.request"]
    assert isinstance(request, DictionaryAttr)
    values = request.data["values"]
    assert isinstance(values, ArrayAttr)
    placed, allocations, peak = [], [], 0
    for value in values:
        assert isinstance(value, DictionaryAttr)
        size, live, name = (value.data[key] for key in ("bytes", "live", "id"))
        if not isinstance(size, IntegerAttr):
            return {"error": "dynamic-storage-size", "phase": "build"}
        assert isinstance(live, ArrayAttr) and isinstance(name, StringAttr)
        begin, end = (bound.value.data for bound in live)
        size, offset = size.value.data, 0
        while True:
            next_offset = offset
            for pbegin, pend, address, count in placed:
                if (size > 0 and count > 0 and begin <= pend and pbegin <= end
                        and offset < address + count and address < offset + size):
                    next_offset = max(next_offset, ((address + count + 15) // 16) * 16)
            if next_offset == offset:
                break
            offset = next_offset
        placed.append((begin, end, offset, size))
        allocations.append({"id": name.data, "offset": offset})
        peak = max(peak, offset + size)
    return {"allocations": allocations, "peak_bytes": peak}
