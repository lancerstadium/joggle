#!/usr/bin/env bash
# Measure complete preparation repeatedly and require deterministic output.
#
# Usage: docs/measure-prepare.sh <semantic.jog> [runs]
set -euo pipefail
cd "$(dirname "$0")/.."

graph="${1:?usage: measure-prepare.sh <semantic.jog> [runs]}"
runs="${2:-3}"
case "$runs" in
  ''|*[!0-9]*) echo "runs must be a positive integer" >&2; exit 2 ;;
esac
if (( runs < 1 )); then
  echo "runs must be a positive integer" >&2
  exit 2
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/joggle-prepare.XXXXXX")
cleanup() { rm -rf "$work"; }
trap cleanup EXIT

times=()
expected=""
for ((run = 1; run <= runs; ++run)); do
  output="$work/output-$run.jog"
  start=$(python3 -c 'import time; print(time.monotonic_ns())')
  build/joggle run c.prepare "$graph" -M build/modules >"$output"
  end=$(python3 -c 'import time; print(time.monotonic_ns())')
  elapsed=$(python3 - "$start" "$end" <<'PY'
import sys
print((int(sys.argv[2]) - int(sys.argv[1])) / 1_000_000_000)
PY
)
  digest=$(shasum -a 256 "$output" | cut -d' ' -f1)
  if [[ -n "$expected" && "$digest" != "$expected" ]]; then
    echo "preparation output changed between runs" >&2
    exit 1
  fi
  expected="$digest"
  times+=("$elapsed")
done

python3 - "$expected" "${times[@]}" <<'PY'
import json, statistics, sys
values = [float(value) for value in sys.argv[2:]]
print(json.dumps({
    "runs": len(values),
    "wall_seconds": values,
    "median_seconds": statistics.median(values),
    "output_sha256": sys.argv[1],
}, indent=2))
PY
