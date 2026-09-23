from xdsl.dialects.builtin import TensorType
from xdsl.dialects.func import CallOp, FuncOp


def is_cast(op):
    if not isinstance(op, CallOp) or len(op.arguments) != 1 or len(op.res) != 1:
        return False
    a, b = op.arguments[0].type, op.res[0].type
    return (isinstance(a, TensorType) and isinstance(b, TensorType) and
            op.callee.root_reference.data == f"cast_{a.element_type}_{b.element_type}")


def lossless(a, b):
    if a.get_shape() != b.get_shape():
        return False
    source, target = str(a.element_type), str(b.element_type)
    bits = {"i8": 8, "i16": 16, "i32": 32, "i64": 64}
    if source in bits and target in bits:
        return bits[target] >= bits[source]
    return source == "f32" and target == "f64"


def transform(module):
    subject = next(op for op in module.ops
                   if isinstance(op, FuncOp) and op.sym_name.data == "subject")
    block = subject.body.block
    for op in list(block.ops):
        if not is_cast(op):
            continue
        value, result = op.arguments[0], op.res[0]
        if value.type == result.type:
            result.replace_all_uses_with(value)
            block.erase_op(op)
        elif is_cast(inner := value.owner):
            original = inner.arguments[0]
            if original.type == result.type and lossless(original.type, value.type):
                result.replace_all_uses_with(original)
                block.erase_op(op)
                if not value.uses:
                    block.erase_op(inner)
