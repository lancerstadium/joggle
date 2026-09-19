#!/usr/bin/env bash
# Report milliseconds per single-call expansion on a graph.
#
# The fallback in expand_with expands calls one at a time, which is where
# preparation spends its time on a large graph. Stopping it after twenty calls and
# timing the whole run gives a figure without waiting for a full preparation. The
# passes ahead of the fallback cost about 2.6 s on SSD-MobileNetV1 and less on the
# smaller graphs; subtract that when comparing graphs.
#
# Usage: docs/measure-expansion.sh <semantic.jog> [calls]
set -euo pipefail
cd "$(dirname "$0")/.."

GRAPH="${1:?usage: measure-expansion.sh <semantic.jog> [calls]}"
N="${2:-20}"
MODULE=modules/opt/lib/lower.jog
cp "$MODULE" /tmp/lower.jog.measure-backup

restore() { cp /tmp/lower.jog.measure-backup "$MODULE"; }
trap restore EXIT

python3 - "$N" <<'PY'
import pathlib, sys
n = sys.argv[1]
p = pathlib.Path("modules/opt/lib/lower.jog")
s = p.read_text()
old = """  var changed = false
  for i in 0..len(calls) {
    if ir.live(calls[i]) {
      changed = ir.expand(m, [calls[i]], [bodies[i]]) || changed
    }
  }
  return changed"""
new = f"""  var changed = false
  var ok = 0
  for i in 0..len(calls) {{
    if ir.live(calls[i]) {{
      if i < {n} {{
        if ir.expand(m, [calls[i]], [bodies[i]]) {{
          ok += 1
          changed = true
        }}
        if i == {n} - 1 {{
          base.assert(false, "MEASURE " + text(ok) + " expansions")
        }}
      }} else {{
        changed = ir.expand(m, [calls[i]], [bodies[i]]) || changed
      }}
    }}
  }}
  return changed"""
if old not in s:
    raise SystemExit("fallback not found in " + str(p))
p.write_text(s.replace(old, new, 1))
PY

rsync -a --exclude '.DS_Store' modules/ build/modules/
START=$(python3 -c 'import time; print(time.time())')
build/joggle run c.prepare "$GRAPH" -M build/modules -M modules >/dev/null 2>/tmp/measure.err || true
END=$(python3 -c 'import time; print(time.time())')
python3 - "$START" "$END" "$N" <<'PY'
import re, sys
start, end, n = float(sys.argv[1]), float(sys.argv[2]), int(sys.argv[3])
err = open("/tmp/measure.err", errors="replace").read()
m = re.search(r"MEASURE (\d+) expansions", err)
total = end - start
if not m:
    print(f"the probe did not fire; run took {total:.1f}s (the fallback may not be reached)")
else:
    print(f"{m.group(1)} expansions in {total:.2f}s -> {total / n * 1000:.1f} ms per expansion")
PY
