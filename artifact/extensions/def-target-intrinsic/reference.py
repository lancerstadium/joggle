"""Native fixed-width signed-byte dot-product operation definition."""

from xdsl.dialects.builtin import VectorType, i8, i32
from xdsl.ir import Dialect
from xdsl.irdl import IRDLOperation, irdl_op_definition, operand_def, result_def
from xdsl.utils.exceptions import VerifyException


@irdl_op_definition
class Dot4I8(IRDLOperation):
    name = "extension.dot4_i8"
    lhs = operand_def()
    rhs = operand_def()
    accumulator = operand_def()
    result = result_def()

    def verify_(self):
        for value in (self.lhs, self.rhs):
            typ = value.type
            if (not isinstance(typ, VectorType) or typ.get_shape() != (4,)
                    or typ.element_type != i8 or any(typ.get_scalable_dims())):
                raise VerifyException("operand-type-mismatch")
        if self.accumulator.type != i32:
            raise VerifyException("operand-type-mismatch")
        if self.result.type != i32:
            raise VerifyException("result-type-mismatch")


def register(context):
    context.load_dialect(Dialect("extension", [Dot4I8], []))


def construct(operands, attributes):
    if len(operands) != 3:
        raise VerifyException("operand-count-mismatch")
    return Dot4I8.create(operands=operands, result_types=[i32], attributes=attributes)
