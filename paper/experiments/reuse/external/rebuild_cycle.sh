#!/bin/sh
# Apply the predeclared ranking patch to pinned TVM, time the incremental compiler
# rebuild, measure the ranked plan, then revert and rebuild to restore the baseline.
set -e
SRC=/Users/lancer/Documents/Item/joggle-study/tvm
OUT=/Users/lancer/Documents/Item/joggle/build-study/tvm-control
FILE=src/relax/transform/static_plan_block_memory.cc
PY="$SRC/.venv/bin/python"
export TVM_LIBRARY_PATH=$SRC/build-make/lib PYTHONPATH=$SRC/python
cd $SRC
echo "tvm_rev=$(git rev-parse HEAD) start=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
git status --short $FILE | grep . && { echo "planner source not clean"; exit 1; } || true
echo "lib_before=$(shasum -a 256 build-make/lib/libtvm_compiler.dylib | cut -c1-16)"
git apply $OUT/tvm-ranking.patch && echo "patch applied"
t0=$(date +%s.%N)
cmake --build build-make --parallel 10 > $OUT/rebuild-ranked.log 2>&1
t1=$(date +%s.%N)
echo "rebuild_ranked_seconds=$(echo "$t1 - $t0" | bc) compiled_units=$(grep -c 'Building CXX' $OUT/rebuild-ranked.log) linked=$(grep -c 'Linking' $OUT/rebuild-ranked.log)"
echo "lib_ranked=$(shasum -a 256 build-make/lib/libtvm_compiler.dylib | cut -c1-16)"
$PY $OUT/plan_ultraface.py --label ranked --opset 13 --out $OUT/ranked.json 2>&1 | grep -v arm_aprofile.cc | tail -3
git checkout -- $FILE && echo "patch reverted"
t2=$(date +%s.%N)
cmake --build build-make --parallel 10 > $OUT/rebuild-revert.log 2>&1
t3=$(date +%s.%N)
echo "rebuild_revert_seconds=$(echo "$t3 - $t2" | bc) compiled_units=$(grep -c 'Building CXX' $OUT/rebuild-revert.log) linked=$(grep -c 'Linking' $OUT/rebuild-revert.log)"
$PY $OUT/plan_ultraface.py --label default-after-revert --opset 13 --out $OUT/default-after-revert.json 2>&1 | grep -v arm_aprofile.cc | tail -3
echo "end=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
