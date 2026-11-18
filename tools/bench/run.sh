#!/bin/bash
# tools/bench/run.sh — one benchmark run: a fresh world (seed "bench") driven along tools/bench/route.json, with the
# per-frame timings written by DQ_BENCH, then summarised by frames.py.
#   tools/bench/run.sh <game binary> <settings.json> <out dir> [base userdir to copy: skins, packs...]
# The video driver comes from the environment (e.g. SDL_VIDEODRIVER=offscreen, or DISPLAY for a real window); the
# GPU from the settings' "gpu" (a vendor:device id) or DRI_PRIME / MESA_VK_DEVICE_SELECT.
set -e
bin=$(readlink -f "$1"); settings=$2; out=$3; base=$4
here=$(cd "$(dirname "$0")" && pwd)
rm -rf "$out"; mkdir -p "$out/ud"
[ -n "$base" ] && cp -r "$base"/. "$out/ud/"
cp "$settings" "$out/ud/settings.json"
DQ_BENCH="$out/frames.csv" timeout 900 "$bin" --userdir "$out/ud" --create Bench --seed bench --smoke-script "$here/route.json" > "$out/stdout.txt" 2>&1 || echo "exit $?"
grep -a -m3 -i "graphics\|vulkan 1\|opengl" "$out/ud/latest.log" || true
python3 "$here/frames.py" "$out/frames.csv" 600 | tee "$out/summary.txt"
