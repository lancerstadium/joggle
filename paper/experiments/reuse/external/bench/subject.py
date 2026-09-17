#!/usr/bin/env python3
"""One fresh-process subject for the same-host UltraFace comparison.

Emits protocol rows (iteration,seconds,checksum) on stdout and validation on
stderr, mirroring paper/scripts/measure_systems.py. Systems: joggle (ctypes on the
emitted C library), tvm (Relax VM on the C-target library), ort (ONNX Runtime,
one intra-op thread). Setup is excluded from timing.
"""
import argparse, ctypes, hashlib, sys, time
import numpy as np

REPO = "/Users/lancer/Documents/Item/joggle"
FIX = f"{REPO}/build-matrix/ultraface-rfb-320"
ap = argparse.ArgumentParser()
ap.add_argument("system", choices=["joggle", "tvm", "ort"])
ap.add_argument("--inner", type=int, default=5)
ap.add_argument("--warmup", type=int, default=1)
ap.add_argument("--joggle-dir", default=f"{REPO}/build-study/derivation/ultraface/original")
ap.add_argument("--tvm-lib", default=f"{REPO}/build-study/tvm-control/ultraface_tvm_O2.dylib")
ap.add_argument("--model", default=f"{REPO}/.cache/onnx-zoo/ultraface-rfb-320.onnx")
a = ap.parse_args()

x = np.fromfile(f"{FIX}/input.bin", dtype=np.float32).reshape(1, 3, 240, 320)
scores_ref = np.fromfile(f"{FIX}/scores.ref.bin", dtype=np.float32)
boxes_ref = np.fromfile(f"{FIX}/boxes.ref.bin", dtype=np.float32)

if a.system == "joggle":
    lib = ctypes.CDLL(f"{a.joggle_dir}/model.dylib")
    entry = lib.model_main
    entry.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_ubyte),
                      ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float)]
    entry.restype = None
    weights = np.fromfile(f"{a.joggle_dir}/weights.bin", dtype=np.uint8)
    scores = np.zeros(8840, np.float32); boxes = np.zeros(17680, np.float32)
    xin = np.ascontiguousarray(x.ravel())
    px = xin.ctypes.data_as(ctypes.POINTER(ctypes.c_float)); pw = weights.ctypes.data_as(ctypes.POINTER(ctypes.c_ubyte))
    ps = scores.ctypes.data_as(ctypes.POINTER(ctypes.c_float)); pb = boxes.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
    def run():
        entry(px, pw, ps, pb)
        return scores, boxes
elif a.system == "tvm":
    import tvm
    from tvm import relax
    rt = tvm.runtime.load_module(a.tvm_lib)
    vm = relax.VirtualMachine(rt, tvm.cpu())
    try:
        dev_x = tvm.runtime.tensor(x, tvm.cpu())
    except AttributeError:
        dev_x = tvm.nd.array(x, tvm.cpu())
    main = vm["main"]
    def run():
        out = main(dev_x)
        return out[0].numpy().ravel(), out[1].numpy().ravel()
else:
    import onnxruntime as ort
    so = ort.SessionOptions(); so.intra_op_num_threads = 1; so.inter_op_num_threads = 1
    so.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    so.log_severity_level = 3
    sess = ort.InferenceSession(a.model, so, providers=["CPUExecutionProvider"])
    feed = {"input": x}
    def run():
        out = sess.run(None, feed)
        return out[0].ravel(), out[1].ravel()

for _ in range(a.warmup):
    run()
rows = []
for i in range(a.inner):
    t0 = time.perf_counter()
    s, b = run()
    dt = time.perf_counter() - t0
    digest = hashlib.sha256(np.ascontiguousarray(s, np.float32).tobytes() + np.ascontiguousarray(b, np.float32).tobytes()).hexdigest()[:16]
    rows.append((i, dt, digest))
se = float(np.max(np.abs(s.astype(np.float32) - scores_ref))); be = float(np.max(np.abs(b.astype(np.float32) - boxes_ref)))
print("iteration,seconds,checksum")
for i, dt, d in rows:
    print(f"{i},{dt:.9f},{d}")
print(f"scores,max_abs_error,{se:.6e},boxes,max_abs_error,{be:.6e},threads,1,{'pass' if se <= 1e-5 and be <= 1e-5 else 'FAIL'}", file=sys.stderr)
