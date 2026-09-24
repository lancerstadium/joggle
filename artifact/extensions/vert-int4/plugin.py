"""Native xDSL discovery for the signed-low-bit feature package."""

from dataclasses import dataclass
from typing import IO, ClassVar

from xdsl.context import Context
from xdsl.dialects.builtin import ModuleOp
from xdsl.passes import ModulePass
from xdsl.universe import Universe
from xdsl.utils.target import Target

from .reference import analyze, transform


@dataclass(frozen=True)
class LowerLowbit(ModulePass):
    name: ClassVar[str] = "study-lowbit-lower"

    def apply(self, ctx: Context, op: ModuleOp) -> None:
        transform(op)


@dataclass(frozen=True)
class EmitLowbit(Target):
    name: ClassVar[str] = "study-lowbit-c"

    def emit(self, ctx: Context, module: ModuleOp, output: IO[str]) -> None:
        output.write(analyze(module)["source"])


UNIVERSE = Universe(all_passes={LowerLowbit.name: lambda: LowerLowbit},
                    all_targets={EmitLowbit.name: lambda: EmitLowbit})
