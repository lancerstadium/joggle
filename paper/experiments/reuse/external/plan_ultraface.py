#!/usr/bin/env python3
"""Storage-planner control on pinned TVM: plan UltraFace RFB-320 and record the plan.

Runs the Relax lowering prefix through StaticPlanBlockMemory on the frozen ONNX
fixture, sums planned `relax.memory.alloc_storage` bytes, times repeated
compilation feedback, and attempts an executed artifact check against the
stored ONNX Runtime references used by the Joggle study. No latency claim.
"""
import argparse, hashlib, json, os, platform, statistics, sys, tempfile, time, traceback
from pathlib import Path

import numpy as np
import onnx
import tvm
from tvm import relax
from tvm.relax.frontend.onnx import from_onnx

REPO = Path("/Users/lancer/Documents/Item/joggle")
FIX = REPO / "build-matrix/ultraface-rfb-320"
MODEL = REPO / ".cache/onnx-zoo/ultraface-rfb-320.onnx"
SHAPE = {"input": [1, 3, 240, 320]}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def import_model(model):
    return from_onnx(model, shape_dict=SHAPE, keep_params_in_input=False)


def planning_prefix():
    passes = []
    try:
        from tvm.relax import backend
        passes += [backend.DispatchSampling(), backend.DispatchSortScan()]
    except Exception:  # keep the prefix documented if unavailable
        pass
    passes += [
        relax.transform.LegalizeOps(),
        relax.transform.RewriteDataflowReshape(),
        relax.transform.ToNonDataflow(),
        relax.transform.RemovePurityChecking(),
        relax.transform.CallTIRRewrite(),
        relax.transform.StaticPlanBlockMemory(),
    ]
    return tvm.transform.Sequential(passes)


def collect(mod):
    alloc_storage = tvm.ir.Op.get("relax.memory.alloc_storage")
    alloc_tensor = tvm.ir.Op.get("relax.builtin.alloc_tensor")
    storages, unplanned = [], 0

    def visit(node):
        nonlocal unplanned
        if isinstance(node, relax.Call):
            if node.op.same_as(alloc_storage):
                size = node.args[0]
                value = size.values[0]
                bytes_ = int(value.value) if type(value).__name__ == "IntImm" else None
                storages.append({"bytes": bytes_, "symbolic": bytes_ is None,
                                 "scope": str(node.args[2].value), "dtype": str(node.args[3].value)})
            elif node.op.same_as(alloc_tensor):
                unplanned += 1
    for gv, func in mod.functions_items():
        if isinstance(func, relax.Function):
            relax.analysis.post_order_visit(func, visit)
    by_dtype = {}
    for s in storages:
        d = by_dtype.setdefault(s["dtype"], {"storages": 0, "bytes": 0, "symbolic": 0})
        d["storages"] += 1
        if s["symbolic"]:
            d["symbolic"] += 1
        else:
            d["bytes"] += s["bytes"]
    return {"storages": len(storages), "planned_bytes": sum(s["bytes"] or 0 for s in storages),
            "by_dtype": by_dtype, "unplanned_alloc_tensor": unplanned, "sizes": sorted(
                (s["bytes"] or -1) for s in storages)}


def check_execution(mod, out_dir):
    record = {"attempted": True}
    try:
        t0 = time.perf_counter()
        target = tvm.target.Target("c")
        ex = relax.build(mod, target=target)
        lib_path = str(Path(out_dir) / "ultraface_tvm.dylib")
        ex.export_library(lib_path)
        t1 = time.perf_counter()
        record["build_seconds"] = t1 - t0
        record["library_bytes"] = os.path.getsize(lib_path)
        rt = tvm.runtime.load_module(lib_path)
        vm = relax.VirtualMachine(rt, tvm.cpu())
        x = np.fromfile(FIX / "input.bin", dtype=np.float32).reshape(1, 3, 240, 320)
        try:
            dev_x = tvm.runtime.tensor(x, tvm.cpu())
        except AttributeError:
            dev_x = tvm.nd.array(x, tvm.cpu())
        outputs = []
        for _ in range(2):
            out = vm["main"](dev_x)
            outputs.append([o.numpy().astype(np.float32) for o in out])
        scores_ref = np.fromfile(FIX / "scores.ref.bin", dtype=np.float32)
        boxes_ref = np.fromfile(FIX / "boxes.ref.bin", dtype=np.float32)
        scores, boxes = outputs[0][0].ravel(), outputs[0][1].ravel()
        if scores.size != scores_ref.size and boxes.size == scores_ref.size:
            scores, boxes = boxes, scores
        record["repeat_identical"] = all(np.array_equal(a, b) for a, b in zip(outputs[0], outputs[1]))
        record["scores"] = {"count": int(scores.size), "max_abs_error": float(np.max(np.abs(scores - scores_ref))),
                            "failed": int(np.sum(np.abs(scores - scores_ref) > 1e-5))}
        record["boxes"] = {"count": int(boxes.size), "max_abs_error": float(np.max(np.abs(boxes - boxes_ref))),
                           "failed": int(np.sum(np.abs(boxes - boxes_ref) > 1e-5))}
        record["passed"] = record["scores"]["failed"] == 0 and record["boxes"]["failed"] == 0 and \
            np.all(np.isfinite(scores)) and np.all(np.isfinite(boxes))
    except Exception as exc:  # record, do not hide, the failure
        record["passed"] = False
        record["error"] = "".join(traceback.format_exception_only(type(exc), exc)).strip()[:2000]
    return record


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--label", required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--repeat", type=int, default=5)
    ap.add_argument("--no-exec", action="store_true")
    ap.add_argument("--opset", type=int, default=0, help="convert the fixture graph with onnx.version_converter before import")
    args = ap.parse_args()
    lib_dir = Path(os.environ.get("TVM_LIBRARY_PATH", Path(tvm.__file__).parent))
    lib = str(next(lib_dir.glob("libtvm_compiler.*")))
    record = {"label": args.label, "workload": "UltraFace RFB-320", "scope": "storage-plan control",
              "tvm_version": tvm.__version__, "tvm_library": lib, "tvm_library_sha256": sha(lib),
              "tvm_library_mtime": time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime(os.path.getmtime(lib))),
              "python": sys.version.split()[0], "numpy": np.__version__, "onnx": onnx.__version__,
              "platform": platform.platform(), "model_sha256": sha(MODEL), "shape": SHAPE,
              "fixture_sha256": {p: sha(FIX / p) for p in ("input.bin", "scores.ref.bin", "boxes.ref.bin")},
              "passes": "DispatchSampling, DispatchSortScan, LegalizeOps, RewriteDataflowReshape, "
                        "ToNonDataflow, RemovePurityChecking, CallTIRRewrite, StaticPlanBlockMemory"}
    model = onnx.load(MODEL)
    record["source_opset"] = [(o.domain, o.version) for o in model.opset_import]
    if args.opset:
        from onnx import version_converter, shape_inference
        model = shape_inference.infer_shapes(version_converter.convert_version(model, args.opset))
        onnx.checker.check_model(model)
        converted = args.out.parent / f"ultraface-rfb-320-opset{args.opset}.onnx"
        onnx.save(model, converted)
        record["converted_model"] = {"path": str(converted), "opset": args.opset, "sha256": sha(converted)}
    import_times, plan_times, plans = [], [], []
    for _ in range(args.repeat):
        t0 = time.perf_counter()
        mod = import_model(model)
        t1 = time.perf_counter()
        planned = planning_prefix()(mod)
        t2 = time.perf_counter()
        import_times.append(t1 - t0)
        plan_times.append(t2 - t1)
        plans.append(collect(planned))
    record["import_seconds"] = import_times
    record["plan_prefix_seconds"] = plan_times
    record["feedback_seconds_median"] = statistics.median(a + b for a, b in zip(import_times, plan_times))
    record["plans_identical"] = all(p == plans[0] for p in plans)
    record["plan"] = plans[0]
    if not args.no_exec:
        out_dir = args.out.parent / f"exec-{args.label}"
        out_dir.mkdir(parents=True, exist_ok=True)
        record["execution"] = check_execution(import_model(model), out_dir)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(record, indent=1, default=lambda o: o.item() if hasattr(o, "item") else str(o)) + "\n")
    dump = lambda o: json.dumps(o, default=lambda v: v.item() if hasattr(v, "item") else str(v))
    print(dump({k: record[k] for k in ("label", "feedback_seconds_median", "plans_identical")}),
          dump({k: v for k, v in record["plan"].items() if k != "sizes"}),
          dump({k: v for k, v in record.get("execution", {}).items() if k not in ("error",)}),
          record.get("execution", {}).get("error", ""))


if __name__ == "__main__":
    main()
