#!/bin/sh
# Clean from-source build of pinned TVM (c7b458e), USE_LLVM=OFF, to measure the
# initial compiler build a source-level planner change requires. Not a benchmark.
set -e
SRC=/Users/lancer/Documents/Item/joggle-study/tvm
BLD=$SRC/build-control
echo "start $(date -u +%Y-%m-%dT%H:%M:%SZ) host=$(hostname) ncpu=$(sysctl -n hw.ncpu) tvm_rev=$(git -C $SRC rev-parse HEAD)"
cmake --version | head -1; clang++ --version | head -1
t0=$(date +%s)
cmake -S $SRC -B $BLD -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DUSE_LLVM=OFF -DUSE_METAL=OFF -DUSE_CPP_RPC=OFF -DUSE_RPC=OFF -DUSE_RANDOM=OFF > $BLD-configure.log 2>&1
t1=$(date +%s); echo "configure_seconds=$((t1-t0))"
cmake --build $BLD --parallel 10 > $BLD-build.log 2>&1 || { echo "BUILD FAILED"; tail -30 $BLD-build.log; exit 1; }
t2=$(date +%s); echo "build_seconds=$((t2-t1)) compiled_units=$(grep -c 'Building CXX' $BLD-build.log)"
ls -la $BLD/lib/*.dylib
echo "end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
