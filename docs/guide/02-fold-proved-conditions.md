# Fold proved conditions

`bounds` can expose an interval proof as an ordinary IR edit without owning
control-flow cleanup:

```sh
joggle run bounds.fold opt.fold opt.basic model.jog \
  -M modules > simplified.jog
```

The first pass replaces only comparisons and logical expressions proved
exactly true or false. The existing optimization passes then select constant
branches, fold now-static loops, and remove dead setup. Omitting them leaves
the constant conditions visible for inspection. Dynamic predicates and
arithmetic whose interval could overflow remain unchanged. The same sequence
removes a provably redundant zero-padding guard from an exposed fixed-shape
Conv while retaining a data-dependent activation branch.

