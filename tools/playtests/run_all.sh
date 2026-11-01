#!/bin/bash
# tools/playtests/run_all.sh — every playtest (or the ones named), one fresh world each, headless; prints a summary.
#   tools/playtests/run_all.sh out.txt [name...]
# Needs Xvfb on :90 (Xvfb :90 -screen 0 1280x720x24 &). Runs a copy of the game so a rebuild mid-run can't change it.
out=${1:-/tmp/playtests.txt}; shift
cd "$(dirname "$0")/../.."
cp build/dokuquest build/dokuquest-pt
names=("$@"); [ ${#names[@]} -eq 0 ] && names=($(ls tools/playtests/*.json | xargs -n1 basename | sed 's/\.json$//'))
: > "$out"
for n in "${names[@]}"; do
  if [ "$n" = persist ]; then   # reopens the world containers.json saved
    DISPLAY=:90 SDL_VIDEO_X11_FORCE_EGL=1 timeout 600 ./build/dokuquest-pt --userdir /tmp/pt/u_containers --world T1 --smoke-script tools/playtests/$n.json >/dev/null 2>&1
    code=$?; rm -rf /tmp/pt/u_persist; mkdir -p /tmp/pt/u_persist; cp /tmp/pt/u_containers/latest.log /tmp/pt/u_persist/
  else
    rm -rf /tmp/pt/u_$n
    DISPLAY=:90 SDL_VIDEO_X11_FORCE_EGL=1 timeout 600 ./build/dokuquest-pt --userdir /tmp/pt/u_$n --create T1 --seed hello --smoke-script tools/playtests/$n.json >/dev/null 2>&1
    code=$?
  fi
  echo "$n exit=$code $(grep -a 'TEST DONE' /tmp/pt/u_$n/latest.log | tail -1)" >> "$out"
  grep -a "TEST FAIL" /tmp/pt/u_$n/latest.log | head -6 >> "$out"
done
echo ALLDONE >> "$out"
