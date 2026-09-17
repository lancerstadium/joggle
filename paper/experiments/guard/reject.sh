#!/bin/sh
# Negative controls for the guard-folding derivation: the recipe must reject an
# ambiguous driver and a preparation helper whose cleanup call is duplicated,
# replaced, or decoupled from the changed flag,
# and must leave the installed C module unchanged.
R=/Users/lancer/Documents/Item/joggle
J=$R/build/joggle
G=$R/build-study/derivation/guard/reject
mkdir -p $G
cd $R
before=$(shasum -a 256 modules/c/module.jog | cut -c1-64)
fail=0
expect() { # name expected-diagnostic command...
  name=$1; want=$2; shift 2
  if "$@" > $G/$name.out 2> $G/$name.err; then echo "$name: NOT rejected"; fail=1
  elif grep -q "$want" $G/$name.err; then echo "$name: rejected ($want)"
  else echo "$name: rejected with unexpected diagnostic: $(tail -1 $G/$name.err)"; fail=1; fi
}
sed 's/return c.prepare(m)/return c.prepare(m) || c.prepare(m)/' paper/experiments/guard/compiler.jog > $G/ambiguous.jog
expect ambiguous "expected one c.prepare call in driver" $J run guard.derive $G/ambiguous.jog -M paper/experiments -M modules -M build/modules
for case in duplicate:'s/  if opt.basic(m) {/  opt.basic(m)\n  if opt.basic(m) {/' \
            removed:'s/  if opt.basic(m) {/  if opt.dce(m, []) {/' \
            flag:'s/  if opt.basic(m) {/  if !opt.basic(m) {/'; do
  name=${case%%:*}; script=${case#*:}
  mkdir -p $G/$name/c
  sed "$script" modules/c/module.jog > $G/$name/c/module.jog
  case $name in
    duplicate) want="expected exactly one opt.basic call";;
    removed) want="expected exactly one opt.basic call";;
    flag) want="no longer drives the changed flag";;
  esac
  expect $name "$want" $J run guard.derive paper/experiments/guard/compiler.jog -M $G/$name -M paper/experiments -M modules -M build/modules
done
after=$(shasum -a 256 modules/c/module.jog | cut -c1-64)
[ "$before" = "$after" ] && echo "installed c module unchanged: $before" || { echo "installed c module CHANGED"; fail=1; }
exit $fail
