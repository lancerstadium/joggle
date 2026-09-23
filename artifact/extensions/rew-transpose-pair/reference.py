from xdsl.dialects.builtin import ArrayAttr, IntegerAttr, TensorType
from xdsl.dialects.func import CallOp, FuncOp


def permutation(op):
    if not isinstance(op, CallOp) or not op.callee.root_reference.data.startswith("transpose_"):
        return None
    if len(op.arguments) != 1 or len(op.res) != 1:
        raise ValueError("invalid-permutation")
    source, result = op.arguments[0].type, op.res[0].type
    attr = op.attributes.get("perm")
    if not isinstance(source, TensorType) or not isinstance(result, TensorType):
        raise ValueError("invalid-permutation")
    if not isinstance(attr, ArrayAttr) or any(not isinstance(i, IntegerAttr) for i in attr):
        raise ValueError("invalid-permutation")
    p = [i.value.data for i in attr]
    shape = source.get_shape()
    if (sorted(p) != list(range(len(shape))) or source.element_type != result.element_type or
            tuple(shape[i] for i in p) != result.get_shape()):
        raise ValueError("invalid-permutation")
    return p


def transform(module):
    subject = next(op for op in module.ops
                   if isinstance(op, FuncOp) and op.sym_name.data == "subject")
    block = subject.body.block
    for op in block.ops:
        permutation(op)
    for op in list(block.ops):
        q = permutation(op)
        if q is None:
            continue
        value = op.arguments[0]
        inner = value.owner
        p = permutation(inner)
        if p is not None and [p[i] for i in q] == list(range(len(p))):
            op.res[0].replace_all_uses_with(inner.arguments[0])
            block.erase_op(op)
            if not value.uses:
                block.erase_op(inner)
