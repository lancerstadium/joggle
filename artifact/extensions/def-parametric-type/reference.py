from xdsl.dialects.builtin import IntAttr
from xdsl.ir import Dialect, ParametrizedAttribute, TypeAttribute
from xdsl.irdl import irdl_attr_definition
from xdsl.utils.exceptions import VerifyException


@irdl_attr_definition
class FixedPoint(ParametrizedAttribute, TypeAttribute):
    name = "extension.fx"
    width: IntAttr
    frac: IntAttr

    @classmethod
    def parse_parameters(cls, parser):
        parser.parse_punctuation("<")
        width = parser.parse_integer()
        parser.parse_punctuation(",")
        frac = parser.parse_integer()
        parser.parse_punctuation(">")
        return [IntAttr(width), IntAttr(frac)]

    def print_parameters(self, printer):
        printer.print_string(f"<{self.width.data},{self.frac.data}>")

    def verify(self):
        if not (2 <= self.width.data <= 32 and 0 <= self.frac.data < self.width.data):
            raise VerifyException("invalid-type-parameter")


def register(context):
    context.load_dialect(Dialect("extension", [], [FixedPoint]))
