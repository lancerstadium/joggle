import math
from xdsl.dialects.builtin import FloatAttr, IntegerAttr
from xdsl.dialects.func import CallOp, FuncOp


def transform(module):
    subject = next(op for op in module.ops
                   if isinstance(op, FuncOp) and op.sym_name.data == "subject")
    block = subject.body.block
    for op in list(block.ops):
        if not isinstance(op, CallOp) or op.callee.root_reference.data != "add":
            continue
        for side in range(2):
            zero = op.arguments[side]
            other = op.arguments[1 - side]
            constant = zero.owner
            if not isinstance(constant, CallOp) or constant.callee.root_reference.data != "splat":
                continue
            if other.type != op.res[0].type or zero.type.element_type != other.type.element_type:
                continue
            value = constant.attributes.get("value")
            if not isinstance(value, (FloatAttr, IntegerAttr)) or value.value.data != 0:
                continue
            if isinstance(value, FloatAttr):
                nsz = op.attributes.get("no_signed_zeros")
                if math.copysign(1, value.value.data) < 0 or not isinstance(nsz, IntegerAttr) or not nsz.value.data:
                    continue
            op.res[0].replace_all_uses_with(other)
            block.erase_op(op)
            if not zero.uses:
                block.erase_op(constant)
            break
