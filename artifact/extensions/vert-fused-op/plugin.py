"""Native xDSL discovery for the quantized-convolution feature package."""

from dataclasses import dataclass
from typing import IO, ClassVar

from xdsl.context import Context
from xdsl.dialects.builtin import ModuleOp
from xdsl.passes import ModulePass
from xdsl.universe import Universe
from xdsl.utils.target import Target

from .reference import analyze, transform


@dataclass(frozen=True)
class FuseQconv(ModulePass):
    name: ClassVar[str] = "study-qconv-fuse"

    def apply(self, ctx: Context, op: ModuleOp) -> None:
        transform(op)


@dataclass(frozen=True)
class EmitQconv(Target):
    name: ClassVar[str] = "study-qconv-c"

    def emit(self, ctx: Context, module: ModuleOp, output: IO[str]) -> None:
        output.write(analyze(module)["source"])


UNIVERSE = Universe(all_passes={FuseQconv.name: lambda: FuseQconv},
                    all_targets={EmitQconv.name: lambda: EmitQconv})
