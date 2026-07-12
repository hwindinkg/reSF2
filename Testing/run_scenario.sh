#!/bin/bash
# run_scenario.sh <scenario_file> <max_frames> <output_prefix>
# Runs one scenario under Xvfb, captures stdout+stderr, prints summary.
set -u
SCN="$1"; MAXF="${2:-3000}"; OUT="${3:-/tmp/scn}"
cd /home/z/reSF2
export DISPLAY=:99
export PATH="/home/z/.local/bin:/home/z/.venv/bin:$PATH"
Testing/ensure_xvfb.sh >/dev/null 2>&1
timeout 60 ./build/bin/resf2_app --assets ./assets \
  --input-script "$SCN" --max-frames "$MAXF" \
  >"${OUT}.log" 2>"${OUT}.err"
EC=$?
echo "exit=$EC log=${OUT}.log err=${OUT}.err"
echo "  [ROOT] count: $(grep -c '\[ROOT\]' ${OUT}.log)"
echo "  [KEY] events: $(grep -c '\[KEY\]' ${OUT}.log)"
echo "  [COMBAT] events: $(grep -c '\[COMBAT\]' ${OUT}.log)"
echo "  scene transitions:"
grep '\[scene\]' ${OUT}.log | head -6 | sed 's/^/    /'
