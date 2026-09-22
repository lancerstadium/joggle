"""Native compiler adapters for the shared ONNX benchmark collector.

Adapters compile the submitted graph and bind inputs once. Output copies and
numerical checks remain outside the timed invocation boundary.
"""

from __future__ import annotations

import hashlib
import subprocess
import time
from pathlib import Path


def tvm_identity(target: dict) -> dict:
    import tvm
    import tvm_ffi

    if target.get("kind") != "llvm":
        raise ValueError("the resident-buffer CPU protocol requires an LLVM CPU target")
    libraries = {}
    for name, library in tvm.base._LOADED_LIBS.items():
        path = Path(library._name).resolve()
        with path.open("rb") as stream:
            libraries[name] = {"path": str(path),
                               "sha256": hashlib.file_digest(stream, "sha256").hexdigest()}
    source = Path(tvm.__file__).resolve().parents[2]
    git = subprocess.run(["git", "rev-parse", "HEAD"], cwd=source,
                         capture_output=True, text=True)
    revision, dirty = None, None
    if git.returncode == 0 and (source / "python/tvm").is_dir():
        revision = git.stdout.strip()
        status = subprocess.run(["git", "status", "--porcelain"], cwd=source,
                                check=True, capture_output=True, text=True)
        dirty = bool(status.stdout.strip())
    return {
        "system": "TVM Relax LLVM", "variant": "tvm-relax-llvm",
        "system_revision": f"tvm-{tvm.__version__}" + (f"@{revision}" if revision else ""),
        "source_revision": revision, "source_dirty": dirty,
        "ffi_version": tvm_ffi.__version__, "libraries": libraries,
        "target": target, "resolved_target": str(tvm.target.Target(target)),
        "pipeline": "default", "tuning_trials": 0,
        "execution_boundary": "Relax VM invoke_stateful; resident CPU inputs; output copies excluded",
    }


class TVMRunner:
    def __init__(self, model: bytes, feeds: dict, target: dict, threads: int):
        import onnx
        import tvm
        from tvm.relax.frontend.onnx import from_onnx

        if target.get("kind") != "llvm":
            raise ValueError("only synchronous LLVM CPU execution is supported")
        configure = tvm.get_global_func("runtime.config_threadpool")
        configure(1, threads)
        proto = onnx.load_model_from_string(model)
        self.names = [output.name for output in proto.graph.output]
        self.stages_ns = {}
        started = time.perf_counter_ns()
        module = from_onnx(proto, shape_dict={name: list(value.shape)
                                            for name, value in feeds.items()},
                           keep_params_in_input=False, sanitize_input_names=False)
        self.stages_ns["import"] = time.perf_counter_ns() - started
        started = time.perf_counter_ns()
        executable = tvm.compile(module, target=tvm.target.Target(target))
        self.stages_ns["compile"] = time.perf_counter_ns() - started
        started = time.perf_counter_ns()
        self.vm = tvm.runtime.vm.VirtualMachine(executable, tvm.cpu())
        arguments = []
        for parameter in module["main"].params:
            arguments.append(tvm.runtime.tensor(feeds[str(parameter.name)], tvm.cpu()))
        self.vm.set_input("main", *arguments)
        self.stages_ns["bind"] = time.perf_counter_ns() - started

    def invoke(self):
        self.vm.invoke_stateful("main")

    def outputs(self):
        values = self.vm.get_outputs("main")
        if not isinstance(values, (tuple, list)):
            values = (values,)
        return [value.numpy() for value in values]
