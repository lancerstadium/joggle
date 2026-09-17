#!/bin/sh
# Derived preparation route: derive c.prepare with guard folding, prepare the
# UltraFace canonical model through the derived procedure, plan, place, emit,
# compile, and compare with the installed route and the manual probe.
set -e
R=/Users/lancer/Documents/Item/joggle
J=$R/build/joggle
G=$R/build-study/derivation/guard
M="-M $R/modules -M $R/build/modules"
mkdir -p $G/modules/prepared $G/derived
cd $R
t0=$(date +%s.%N)
$J run guard.derive paper/experiments/guard/compiler.jog -M paper/experiments $M > $G/modules/prepared/module.jog
t1=$(date +%s.%N); echo "derive_seconds=$(echo "$t1-$t0"|bc)"
$J run prepared.apply build-study/ultraface-block/canonical.jog -M $G/modules -M paper/experiments $M > $G/derived/prepared.jog
t2=$(date +%s.%N); echo "derived_prepare_seconds=$(echo "$t2-$t1"|bc)"
$J run mem.plan $G/derived/prepared.jog $M > $G/derived/planned.jog
$J run c.place $G/derived/planned.jog --arg '"static"' $M > $G/derived/placed.jog
$J emit c.source $G/derived/placed.jog --arg '"weights"' $M > $G/derived/model.c
$J emit c.header $G/derived/placed.jog --arg '"weights"' $M > $G/derived/model.h
$J emit c.data $G/derived/placed.jog $M > $G/derived/weights.bin
t3=$(date +%s.%N); echo "plan_place_emit_seconds=$(echo "$t3-$t2"|bc)"
cd $G/derived && clang -std=c99 -O2 -Wall -Wextra -Werror -fPIC -c model.c -o model.o && clang -dynamiclib model.o -lm -o model.dylib
echo "if_count_installed=$(grep -c 'if (' $R/build-study/derivation/ultraface/original/model.c) if_count_derived=$(grep -c 'if (' model.c)"
cmp -s model.c $R/build-study/tvm-control/bounds-probe/model3.c && echo "matches_manual_probe=true" || echo "matches_manual_probe=false"
cmp -s weights.bin $R/build-study/derivation/ultraface/original/weights.bin && echo "weights_identical=true" || echo "weights_identical=false"
shasum -a 256 $R/modules/c/module.jog | cut -c1-16
