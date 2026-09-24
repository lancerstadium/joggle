"""Native compiler adapters for the shared ONNX benchmark collector.

Adapters compile the submitted graph and bind inputs once. Output copies and
numerical checks remain outside the timed invocation boundary.
"""

from __future__ import annotations

import ctypes
import hashlib
import json
import select
import subprocess
import tempfile
import time
from pathlib import Path


ONNX_MLIR_FLAGS = ("-O3", "--parallel=false", "--enable-fast-math=false", "--EmitLib")


class JoggleCompiler:
    """Resident native environment; each request lowers a fresh source graph."""

    def __init__(self, server: Path, modules: Path, timeout: float):
        self.timeout = timeout
        self.errors = tempfile.TemporaryFile(mode="w+t")
        self.process = None
        try:
            self.process = subprocess.Popen(
                [str(server.resolve()), "--compile-server", str(modules.resolve())],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.errors,
                text=True, bufsize=1)
            if self.receive() != {"ready": True}:
                raise RuntimeError("invalid resident compiler greeting")
        except BaseException:
            self.close()
            raise

    def receive(self):
        if not select.select([self.process.stdout], [], [], self.timeout)[0]:
            raise TimeoutError("resident compiler response timed out")
        line = self.process.stdout.readline()
        if not line:
            self.errors.seek(0)
            raise RuntimeError("resident compiler stopped: " + self.errors.read())
        return json.loads(line)

    def compile(self, source: Path, output: Path):
        self.process.stdin.write(json.dumps({"source": str(source.resolve()),
                                            "output": str(output.resolve())}) + "\n")
        self.process.stdin.flush()
        if self.receive().get("ok") is not True:
            raise RuntimeError("resident compiler rejected source")

    def close(self):
        if self.process is not None:
            try:
                self.process.stdin.close()
            except BrokenPipeError:
                pass
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
            self.process.stdout.close()
            self.process = None
        self.errors.close()


class JoggleRunner:
    def __init__(self, model: bytes, feeds: dict, compiler: JoggleCompiler,
                 tool: Path, modules: Path, cc: str, timeout: float):
        import numpy as np
        import onnx
        from joggle_entry import signature_command
        from run_joggle_benchmarks import run_to_file

        self._temporary = tempfile.TemporaryDirectory(prefix="joggle-native-update-")
        self._lib = None
        self._invoked = False
        self.stages_ns = {}
        try:
            root = Path(self._temporary.name)
            proto = onnx.load_model_from_string(model)
            self.names = [item.name for item in proto.graph.output]
            constants = {item.name for item in proto.graph.initializer}
            inputs = [item.name for item in proto.graph.input if item.name not in constants]
            if set(inputs) != set(feeds):
                raise ValueError("input names differ from ONNX graph")
            path = root / "model.onnx"
            path.write_bytes(model)
            started = time.perf_counter_ns()
            run_to_file([tool, "read", "onnx.read", path, "-M", modules],
                        root / "read.jog", "onnx.read", timeout)
            signature = [{"dtype": feeds[name].dtype.name, "shape": list(feeds[name].shape)}
                         for name in inputs]
            run_to_file(signature_command(tool, root / "read.jog", modules, signature),
                        root / "source.jog", "opt.signature", timeout)
            self.stages_ns["decode_specialize"] = time.perf_counter_ns() - started
            started = time.perf_counter_ns()
            output = root / "compiled"
            compiler.compile(root / "source.jog", output)
            self.stages_ns["lower_emit"] = time.perf_counter_ns() - started
            started = time.perf_counter_ns()
            library = root / "model.so"
            compiled = subprocess.run([cc, "-std=c11", "-O3", "-DNDEBUG", "-shared", "-fPIC",
                                       str(output / "0.c"), "-lm", "-o", str(library)],
                                      capture_output=True, text=True, timeout=timeout)
            if compiled.returncode:
                raise RuntimeError(f"Joggle host compilation failed (exit {compiled.returncode}):\n"
                                   f"{compiled.stdout}\n{compiled.stderr}")
            self.stages_ns["host_compile"] = time.perf_counter_ns() - started
            started = time.perf_counter_ns()
            api = json.loads((output / "0.api.json").read_text())
            entries = [entry for entry in api if entry["name"] == "model_main"]
            if len(entries) != 1:
                raise ValueError("missing generated model_main ABI")
            entry = entries[0]
            if len(entry["params"]) != len(inputs) or len(entry["results"]) != len(self.names):
                raise ValueError("generated ABI arity differs from ONNX graph")
            dtypes = {"float": "float32", "double": "float64", "bool": "bool",
                      **{f"{sign}int{bits}_t": f"{sign}int{bits}"
                         for sign in ("", "u") for bits in (8, 16, 32, 64)}}
            self._arrays, self._outputs, self._arguments, argument_types = [], [], [], []
            for name, parameter in zip(inputs, entry["params"], strict=True):
                array = np.ascontiguousarray(feeds[name])
                if (not parameter["pointer"] or parameter["shape"] != list(array.shape)
                        or dtypes.get(parameter["c"]) != array.dtype.name):
                    raise ValueError("unsupported generated input ABI")
                self._arrays.append(array)
                self._arguments.append(array.ctypes.data)
                argument_types.append(ctypes.c_void_p)
            # The emitter's ABI places its immutable data pointer between inputs
            # and results. Own aligned storage for the whole runner lifetime;
            # a replacement runner must not overwrite a live predecessor's data.
            if entry["data"]:
                payload = (output / "0.bin").read_bytes()
                weights = np.zeros(max(1, len(payload)), dtype=np.uint8)
                weights[:len(payload)] = np.frombuffer(payload, dtype=np.uint8)
                self._arrays.append(weights)
                self._arguments.append(weights.ctypes.data)
                argument_types.append(ctypes.c_void_p)
            for result in entry["results"]:
                dtype = np.dtype(dtypes[result["c"]])
                size = result["bytes"]
                if not result["pointer"] or type(size) is not int or size < 0 or size % dtype.itemsize:
                    raise ValueError("unsupported generated output ABI")
                array = np.zeros(max(1, size // dtype.itemsize), dtype=dtype)
                self._arguments.append(array.ctypes.data)
                argument_types.append(ctypes.c_void_p)
                dims = []
                for extent in result["shape"]:
                    if extent == "_":
                        extent = ctypes.c_int64(-1)
                        self._arguments.append(ctypes.byref(extent))
                        argument_types.append(ctypes.POINTER(ctypes.c_int64))
                    dims.append(extent)
                self._outputs.append((array, dims, size // dtype.itemsize))
            self._lib = ctypes.CDLL(str(library))
            self._entry = self._lib.model_main
            self._entry.argtypes, self._entry.restype = argument_types, None
            self.stages_ns["bind"] = time.perf_counter_ns() - started
        except BaseException:
            self.close()
            raise

    def invoke(self):
        if self._lib is None:
            raise RuntimeError("runner is closed")
        self._entry(*self._arguments)
        self._invoked = True

    def outputs(self):
        import math
        if self._lib is None or not self._invoked:
            raise RuntimeError("invoke a live runner before reading outputs")
        values = []
        for array, dims, capacity in self._outputs:
            shape = tuple(dim.value if isinstance(dim, ctypes.c_int64) else dim for dim in dims)
            if any(type(dim) is not int or dim < 0 for dim in shape) or math.prod(shape) > capacity:
                raise ValueError("generated result shape exceeds allocated capacity")
            values.append(array[:math.prod(shape)].reshape(shape).copy())
        return values

    def close(self):
        self._lib = None
        self._entry = None
        self._temporary.cleanup()


def onnx_mlir_identity(compiler: Path) -> dict:
    compiler = compiler.resolve(strict=True)
    version = subprocess.run([str(compiler), "--version"], check=True,
                             capture_output=True, text=True).stdout.strip()
    source = next((parent for parent in compiler.parents
                   if (parent / "src/Compiler/CompilerUtils.cpp").is_file()), None)
    revision, dirty = None, None
    if source:
        revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=source,
                                  check=True, capture_output=True, text=True).stdout.strip()
        dirty = bool(subprocess.run(["git", "status", "--porcelain"], cwd=source,
                                    check=True, capture_output=True, text=True).stdout.strip())
    artifacts = {}
    for path in (compiler, compiler.parent.parent / "lib/libcruntime.a"):
        with path.open("rb") as stream:
            artifacts[path.name] = {"path": str(path),
                                   "sha256": hashlib.file_digest(stream, "sha256").hexdigest()}
    return {
        "system": "ONNX-MLIR LLVM", "variant": "onnx-mlir-llvm",
        "system_revision": version.splitlines()[0], "version": version,
        "source_revision": revision, "source_dirty": dirty, "artifacts": artifacts,
        "compile_flags": list(ONNX_MLIR_FLAGS), "shape_specialization": "fixed benchmark inputs",
        "execution_boundary": "C ABI run_main_graph with resident inputs; includes prior output release and native output allocation; copies excluded",
    }


class ONNXMLIRRunner:
    """Native C ABI, retaining input storage and releasing every output list."""

    def __init__(self, model: bytes, feeds: dict, compiler: Path, threads: int):
        import numpy as np
        import onnx

        if threads != 1:
            raise ValueError("ONNX-MLIR adapter uses the single-threaded CPU protocol")
        self._temporary = tempfile.TemporaryDirectory(prefix="onnx-mlir-benchmark-")
        self._input = self._output = None
        self._lib = None
        self.stages_ns = {}
        try:
            root = Path(self._temporary.name)
            proto = onnx.load_model_from_string(model)
            self.names = [output.name for output in proto.graph.output]
            initializers = {value.name for value in proto.graph.initializer}
            inputs = [value for value in proto.graph.input if value.name not in initializers]
            if set(feeds) != {value.name for value in inputs}:
                raise ValueError("input names differ from the submitted ONNX graph")
            self._arrays = [np.ascontiguousarray(feeds[value.name]) for value in inputs]
            shapes = ",".join(f"{i}:" + "x".join(map(str, array.shape))
                              for i, array in enumerate(self._arrays) if array.ndim)
            path = root / "model.onnx"
            path.write_bytes(model)
            command = [str(compiler.resolve()), *ONNX_MLIR_FLAGS, str(path),
                       "-o", str(root / "model")]
            if shapes:
                command.append("--shapeInformation=" + shapes)
            started = time.perf_counter_ns()
            compiled = subprocess.run(command, capture_output=True, text=True)
            if compiled.returncode:
                raise RuntimeError(f"ONNX-MLIR compilation failed:\n{compiled.stdout}\n{compiled.stderr}")
            self.stages_ns["compile"] = time.perf_counter_ns() - started
            started = time.perf_counter_ns()
            self._lib = ctypes.CDLL(str(root / "model.so"))
            ptr, i64 = ctypes.c_void_p, ctypes.c_int64
            signatures = {
                "omTensorCreate": (ptr, [ptr, ctypes.POINTER(i64), i64, ctypes.c_int]),
                "omTensorDestroy": (None, [ptr]),
                "omTensorListCreate": (ptr, [ctypes.POINTER(ptr), i64]),
                "omTensorListDestroy": (None, [ptr]),
                "omTensorListGetSize": (i64, [ptr]),
                "omTensorListGetOmtByIndex": (ptr, [ptr, i64]),
                "omTensorGetDataPtr": (ptr, [ptr]),
                "omTensorGetRank": (i64, [ptr]),
                "omTensorGetShape": (ctypes.POINTER(i64), [ptr]),
                "omTensorGetStrides": (ctypes.POINTER(i64), [ptr]),
                "omTensorGetDataType": (ctypes.c_int, [ptr]),
                "run_main_graph": (ptr, [ptr]),
            }
            for name, (result, arguments) in signatures.items():
                function = getattr(self._lib, name)
                function.restype, function.argtypes = result, arguments
            tensors = []
            try:
                for array in self._arrays:
                    shape = (i64 * array.ndim)(*array.shape)
                    tensor = self._lib.omTensorCreate(array.ctypes.data, shape, array.ndim,
                                                     onnx.helper.np_dtype_to_tensor_dtype(array.dtype))
                    if not tensor:
                        raise MemoryError("omTensorCreate returned null")
                    tensors.append(tensor)
                self._input = self._lib.omTensorListCreate((ptr * len(tensors))(*tensors), len(tensors))
                if not self._input:
                    raise MemoryError("omTensorListCreate returned null")
            except BaseException:
                for tensor in tensors:
                    self._lib.omTensorDestroy(tensor)
                raise
            self.stages_ns["bind"] = time.perf_counter_ns() - started
        except BaseException:
            self.close()
            raise

    def invoke(self):
        if not self._input:
            raise RuntimeError("runner is closed")
        if self._output:
            self._lib.omTensorListDestroy(self._output)
            self._output = None
        self._output = self._lib.run_main_graph(self._input)
        if not self._output:
            raise RuntimeError("ONNX-MLIR returned a null output list")

    def outputs(self):
        import numpy as np
        import onnx

        if not self._output:
            raise RuntimeError("invoke before reading outputs")
        values = []
        for i in range(self._lib.omTensorListGetSize(self._output)):
            tensor = self._lib.omTensorListGetOmtByIndex(self._output, i)
            rank = self._lib.omTensorGetRank(tensor)
            shape = tuple(self._lib.omTensorGetShape(tensor)[j] for j in range(rank))
            dtype = np.dtype(onnx.helper.tensor_dtype_to_np_dtype(self._lib.omTensorGetDataType(tensor)))
            if dtype.hasobject or any(d < 0 for d in shape):
                raise ValueError("unsupported output element type or negative extent")
            if any(d == 0 for d in shape):
                values.append(np.empty(shape, dtype=dtype))
                continue
            strides = tuple(self._lib.omTensorGetStrides(tensor)[j] * dtype.itemsize for j in range(rank))
            low = sum(min(0, (d - 1) * s) for d, s in zip(shape, strides))
            high = sum(max(0, (d - 1) * s) for d, s in zip(shape, strides)) + dtype.itemsize
            data = self._lib.omTensorGetDataPtr(tensor)
            if not data:
                raise RuntimeError("nonempty output has null storage")
            buffer = (ctypes.c_char * (high - low)).from_address(data + low)
            values.append(np.ndarray(shape, dtype=dtype, buffer=buffer, offset=-low,
                                     strides=strides).copy())
        return values

    def close(self):
        if self._lib:
            for name in ("_output", "_input"):
                pointer = getattr(self, name)
                if pointer:
                    self._lib.omTensorListDestroy(pointer)
                    setattr(self, name, None)
        self._temporary.cleanup()


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
