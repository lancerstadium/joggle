"""Native signed-int4 validation and widening rewrite; emission is separate."""
from xdsl.dialects.builtin import IntegerAttr, TensorType, i8
from xdsl.dialects.func import CallOp, FuncOp


def transform(module):
    subject = next(op for op in module.ops
                   if isinstance(op, FuncOp) and op.sym_name.data == "subject")
    block = subject.body.block
    calls = [op for op in block.ops if isinstance(op, CallOp)]
    for op in calls:
        if op.callee.root_reference.data == "qint4_literal":
            for value in op.attributes["values"]:
                if not isinstance(value, IntegerAttr) or not -8 <= value.value.data <= 7:
                    raise ValueError("literal-out-of-range")
        if op.callee.root_reference.data == "qint4_add":
            if len(op.arguments) != 2 or len(op.res) != 1:
                raise ValueError("arity-mismatch")
            ty = op.res[0].type
            if not isinstance(ty, TensorType) or str(ty.element_type) != "i4":
                raise ValueError("unsupported-element-type")
            if any(value.type != ty for value in op.arguments):
                raise ValueError("shape-mismatch")
    for op in calls:
        if op.callee.root_reference.data != "qint4_add":
            continue
        ty = op.res[0].type
        wide = TensorType(i8, ty.get_shape())

        def call(name, operands, result_type):
            result = CallOp(name, operands, [result_type])
            block.insert_op_before(result, op)
            return result

        left = call("sext_i4_i8", [op.arguments[0]], wide)
        right = call("sext_i4_i8", [op.arguments[1]], wide)
        summed = call("add_i8", [left.res[0], right.res[0]], wide)
        clamped = call("clamp_i8", [summed.res[0]], wide)
        clamped.attributes["lower"] = IntegerAttr(-8, 64)
        clamped.attributes["upper"] = IntegerAttr(7, 64)
        narrowed = call("trunc_i8_i4", [clamped.res[0]], ty)
        op.res[0].replace_all_uses_with(narrowed.res[0])
        block.erase_op(op)


def analyze(module):
    subject = next(op for op in module.ops
                   if isinstance(op, FuncOp) and op.sym_name.data == "subject")
    clamps = [op for op in subject.body.block.ops if isinstance(op, CallOp)
              and op.callee.root_reference.data == "clamp_i8"]
    if len(clamps) != 1:
        raise ValueError("expected-lowered-int4")
    low, high = (clamps[0].attributes[key].value.data for key in ("lower", "upper"))
    source = "#include <stdint.h>\n#include <stddef.h>\n"
    source += f"#define LOW ({low})\n#define HIGH ({high})\n"
    source += """
int task_kernel(const int8_t *a, const int8_t *b, int8_t *values,
                uint8_t *packed, size_t n) {
  for (size_t i = 0; i < n; ++i)
    if (a[i] < -8 || a[i] > 7 || b[i] < -8 || b[i] > 7) return 1;
  for (size_t i = 0; i < n; ++i) {
    int sum = (int)a[i] + (int)b[i];
    if (sum < LOW) sum = LOW;
    if (sum > HIGH) sum = HIGH;
    values[i] = (int8_t)sum;
    uint8_t nibble = (uint8_t)((unsigned)sum & 15u);
    if ((i & 1u) == 0) packed[i / 2] = nibble;
    else packed[i / 2] |= (uint8_t)(nibble << 4);
  }
  return 0;
}
"""
    return {"range": [low, high], "symbol": "task_kernel", "source": source}
