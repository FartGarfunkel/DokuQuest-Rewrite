#!/bin/bash
# run the named playtests one after another: runsome.sh out.txt name...
out=$1; shift
cd /home/claude/dq
: > $out
for n in "$@"; do
  if [ $n = persist ]; then   # reopens the world containers.json saved
    cp /tmp/pt/u_containers/latest.log /tmp/pt/u_containers/c.log 2>/dev/null
    DISPLAY=:90 SDL_VIDEO_X11_FORCE_EGL=1 timeout 600 ./build/dokuquest --userdir /tmp/pt/u_containers --world T1 --smoke-script tools/playtests/$n.json >/dev/null 2>&1
    rm -rf /tmp/pt/u_persist; mkdir -p /tmp/pt/u_persist; cp /tmp/pt/u_containers/latest.log /tmp/pt/u_persist/
  else
  rm -rf /tmp/pt/u_$n
  DISPLAY=:90 SDL_VIDEO_X11_FORCE_EGL=1 timeout 600 ./build/dokuquest --userdir /tmp/pt/u_$n --create T1 --seed hello --smoke-script tools/playtests/$n.json >/dev/null 2>&1
  fi
  echo "$n exit=$? $(grep -a 'TEST DONE' /tmp/pt/u_$n/latest.log | tail -1)" >> $out
  grep -a "TEST FAIL" /tmp/pt/u_$n/latest.log | head -6 >> $out
done
echo ALLDONE >> $out
