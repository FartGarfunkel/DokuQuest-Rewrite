#!/usr/bin/env bash
# tools/android/emulator_test.sh — the Android build in an emulator (the Android workflow's "emulator" job, with
# the x86_64 APK from ABI=x86_64 tools/android/build_apk.sh):
#   1. install, first start: the game's files are unpacked, Vulkan starts, the title screen shows (screenshot);
#   2. a new world played for a few hundred frames at low settings, then a screenshot, as the desktop smoke test does.
# What happened goes into the job's notices and errors, the screenshots into the log as small JPEGs (base64, between
# "=== screenshot <name>" and "=== end"), and the whole log + logcat into emulator-test/.
set -uo pipefail
cd "$(dirname "$0")/../.."
APK="$(ls dist/android/*-android-x86_64.apk | head -1)"
PKG=org.dokuquest.app
DIR=/sdcard/Android/data/$PKG/files
OUT=emulator-test
mkdir -p "$OUT"
rc=0
note() { echo "::notice::emulator $(date +%H:%M:%S): $*"; }
err() { echo "::error::emulator $(date +%H:%M:%S): $*"; rc=1; }
alive() { [ -n "$(timeout 20 adb shell pidof $PKG 2>/dev/null | tr -d '\r')" ]; }
shot() {   # a screenshot of the screen as it is now, into the log
  timeout 30 adb exec-out screencap -p > "$OUT/$1.png" 2>/dev/null || return
  python3 - "$OUT/$1.png" "$1" <<'EOF' || true
import base64, io, sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
w, h = im.size
px = list(im.resize((64, 36)).getdata())
lit = sum(1 for p in px if max(p) > 24) / len(px)
print("::notice::emulator: screenshot %s %dx%d, %.0f%% of it not black" % (sys.argv[2], w, h, lit * 100))
im.thumbnail((800, 800))
b = io.BytesIO(); im.save(b, "JPEG", quality=70)
print("=== screenshot " + sys.argv[2])
s = base64.b64encode(b.getvalue()).decode()
for i in range(0, len(s), 120): print(s[i:i + 120])
print("=== end")
EOF
}
logs() { timeout 60 adb logcat -d -v brief > "$OUT/logcat-$1.txt" 2>/dev/null; }
crashed() {   # anything in logcat that means this app died (other processes' errors are not ours)
  grep -E -A12 "Process: $PKG|Fatal signal.*\($PKG\)|>>> $PKG <<<|E/DokuQuest.*(crashed|uncaught)" "$OUT/logcat-$1.txt" | head -40
}
game_log() { grep -E "[VIWEF]/(DokuQuest|SDL)" "$OUT/logcat-$1.txt" | head -${2:-80}; }   # (V/SDL: the activity and its surface coming and going)

echo "Android $(adb shell getprop ro.build.version.release | tr -d '\r'), ABIs $(adb shell getprop ro.product.cpu.abilist | tr -d '\r'), Vulkan HAL $(adb shell getprop ro.hardware.vulkan | tr -d '\r')"
adb install -r "$APK" || { err "the APK did not install"; exit 1; }
# (no "Viewing full screen" tip over the screenshots)
adb shell settings put secure immersive_mode_confirmations confirmed
note "installed $(basename "$APK")"

# ---- 1. first start: unpacking, Vulkan, the title screen
adb logcat -c
t0=$(date +%s)
timeout 180 adb shell am start -W -n $PKG/.MainActivity
state=""
for i in $(seq 1 120); do   # (up to 10 minutes: the emulator's GPU is software, so loading is slow)
  sleep 5
  logs first
  if [ -z "${LOADSHOT:-}" ] && grep -q "I/DokuQuest.*swapchain" "$OUT/logcat-first.txt" && ! grep -q "first frame drawn" "$OUT/logcat-first.txt"; then LOADSHOT=1; sleep 2; shot loading; fi
  if grep -q "I/DokuQuest.*first frame drawn" "$OUT/logcat-first.txt"; then state=vulkan; grep -qE "I/DokuQuest.*Vulkan 1\.3 \(" "$OUT/logcat-first.txt" || state=gles; break; fi
  if [ -n "$(crashed first)" ]; then state=error; sleep 5; break; fi
  if ! alive && [ $i -gt 2 ]; then state=gone; break; fi
done
sleep 10   # (the title screen a few frames in)
logs first
grep -E "I/DokuQuest.*first frame drawn" "$OUT/logcat-first.txt" | head -1 | while IFS= read -r l; do note "${l:0:200}"; done
echo "::group::game log, first start"; game_log first 120; echo "::endgroup::"
grep -E "I/DokuQuest.*(unpack|files unpacked|game files in|Vulkan 1\.3 \(|swapchain)" "$OUT/logcat-first.txt" | sed 's/^/  /' | while IFS= read -r l; do note "${l:0:300}"; done
grep -E "E/DokuQuest" "$OUT/logcat-first.txt" | sort -u | head -20 | while IFS= read -r l; do echo "::warning::emulator: ${l:0:300}"; done
c=$(crashed first); [ -n "$c" ] && { err "crash on the first start"; echo "$c"; }
alive && note "still running after $(( $(date +%s) - t0 )) s (state: ${state:-waiting})" || err "the game is not running after the first start (state: ${state:-none})"
shot title
VULKAN_OK=0; [ "$state" = vulkan ] && VULKAN_OK=1

# ---- 1b. away and back (Home, then the app again): Android takes the window's surface and gives a new one
if [ $VULKAN_OK = 1 ] && alive; then
  adb logcat -c
  adb shell input keyevent KEYCODE_HOME; sleep 6
  timeout 60 adb shell am start -n $PKG/.MainActivity > /dev/null
  for i in $(seq 1 24); do sleep 5; logs resume; grep -q "vulkan: new window surface" "$OUT/logcat-resume.txt" && break; alive || break; done
  sleep 5; logs resume
  c=$(crashed resume); [ -n "$c" ] && { err "crash on coming back to the app"; echo "$c"; }
  if alive && grep -q "vulkan: new window surface" "$OUT/logcat-resume.txt"; then note "away and back: a new Vulkan surface, still running"; else err "away and back: not running, or no new Vulkan surface"; fi
  grep -E "[EW]/DokuQuest" "$OUT/logcat-resume.txt" | head -10
  shot after-resume
fi
timeout 30 adb shell am force-stop $PKG
sleep 2

# ---- 2. a world at low settings (the emulator draws with a software GPU), 300 frames, a screenshot
if [ $VULKAN_OK = 1 ]; then
  echo '{"preset":"custom","renderer":"vulkan","renderDist":4,"farTerrain":256,"shadows":0,"waterQuality":0,"clouds":false,"portalViews":false,"scale":0.5,"particles":1,"weatherParticles":0,"microVoxels":0,"plantDetail":"flat","shaderLevel":0,"fancyLeaves":false,"volumetricClouds":false}' > "$OUT/settings.json"
  timeout 60 adb push "$OUT/settings.json" $DIR/settings.json > /dev/null || err "could not write settings.json into the game's folder"
  adb shell rm -f $DIR/shot.png
  adb logcat -c
  t0=$(date +%s)
  timeout 180 adb shell am start -W -n $PKG/.MainActivity --esa args "--create,Test,--seed,hello,--smoke-frames,300,--smoke-shot,$DIR/shot.png"
  for i in $(seq 1 240); do sleep 5; alive || break; [ $((i % 24)) = 0 ] && shot "world-$((i / 12))min"; done
  logs world
  secs=$(( $(date +%s) - t0 ))
  echo "::group::game log, world"; game_log world 200; echo "::endgroup::"
  c=$(crashed world); [ -n "$c" ] && { err "crash in the world run"; echo "$c"; }
  if alive; then err "the world run did not finish its 300 frames in $secs s"; shot world-stuck; timeout 30 adb shell am force-stop $PKG; fi
  timeout 60 adb pull $DIR/latest.log "$OUT/latest.log" > /dev/null 2>&1 && { echo "::group::latest.log"; tail -150 "$OUT/latest.log"; echo "::endgroup::"; }
  grep -E "^\[error\]" "$OUT/latest.log" 2>/dev/null | sort -u | head -20 | while IFS= read -r l; do echo "::warning::emulator latest.log: ${l:0:300}"; done
  if timeout 60 adb pull $DIR/shot.png "$OUT/world.png" > /dev/null 2>&1; then
    note "the world ran 300 frames in $secs s and saved its screenshot"
    python3 - "$OUT/world.png" <<'EOF' || true
import base64, io, sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
print("::notice::emulator: game screenshot %dx%d" % im.size)
im.thumbnail((800, 800)); b = io.BytesIO(); im.save(b, "JPEG", quality=75)
print("=== screenshot world-game"); s = base64.b64encode(b.getvalue()).decode()
for i in range(0, len(s), 120): print(s[i:i + 120])
print("=== end")
EOF
  else
    err "no screenshot from the world run"
  fi
else
  note "the emulator's Vulkan can't run the game (see the log above); the world run is skipped"
fi

# ---- 3. the same world on OpenGL ES (what a phone without Vulkan 1.3 gets): reopened, 200 frames, a screenshot
timeout 30 adb shell am force-stop $PKG; sleep 2
adb shell rm -f $DIR/shot-gles.png
adb logcat -c
t0=$(date +%s)
WORLD=--create,TestGles; [ $VULKAN_OK = 1 ] && WORLD=--world,Test
timeout 180 adb shell am start -W -n $PKG/.MainActivity --esa args "--renderer,legacy,$WORLD,--smoke-frames,200,--smoke-shot,$DIR/shot-gles.png"
for i in $(seq 1 240); do sleep 5; alive || break; [ $((i % 24)) = 0 ] && shot "gles-$((i / 12))min"; done
logs gles
secs=$(( $(date +%s) - t0 ))
echo "::group::game log, OpenGL ES"; game_log gles 150; echo "::endgroup::"
grep -E "I/DokuQuest.*OpenGL ES" "$OUT/logcat-gles.txt" | head -2 | while IFS= read -r l; do note "${l:0:300}"; done
grep -E "[EW]/DokuQuest" "$OUT/logcat-gles.txt" | sort -u | head -20 | while IFS= read -r l; do echo "::warning::emulator gles: ${l:0:300}"; done
c=$(crashed gles); [ -n "$c" ] && { err "crash in the OpenGL ES run"; echo "$c"; }
if alive; then err "the OpenGL ES run did not finish its 200 frames in $secs s"; shot gles-stuck; timeout 30 adb shell am force-stop $PKG; fi
if timeout 60 adb pull $DIR/shot-gles.png "$OUT/gles.png" > /dev/null 2>&1; then
  note "OpenGL ES: the world ran 200 frames in $secs s and saved its screenshot"
  python3 - "$OUT/gles.png" <<'PYEOF' || true
import base64, io, sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
im.thumbnail((800, 800)); b = io.BytesIO(); im.save(b, "JPEG", quality=75)
print("=== screenshot gles-game"); s = base64.b64encode(b.getvalue()).decode()
for i in range(0, len(s), 120): print(s[i:i + 120])
print("=== end")
PYEOF
else
  err "no screenshot from the OpenGL ES run"
fi

# ---- 3a. a phone without Vulkan 1.3 (Android 12 and older, e.g. a Galaxy Note 10+): the window is made for Vulkan,
# Vulkan turns it down, and the game makes a window for OpenGL ES instead (--renderer vulkan-missing acts that out)
timeout 30 adb shell am force-stop $PKG; sleep 2
adb shell rm -f $DIR/shot-novk.png
adb logcat -c
TW=--world,TestGles; [ $VULKAN_OK = 1 ] && TW=--world,Test
timeout 180 adb shell am start -W -n $PKG/.MainActivity --esa args "--renderer,vulkan-missing,$TW,--smoke-frames,120,--smoke-shot,$DIR/shot-novk.png"
for i in $(seq 1 120); do sleep 5; alive || break; done
logs novk
c=$(crashed novk); [ -n "$c" ] && { err "crash in the no-Vulkan run"; echo "$c"; }
if alive; then err "the no-Vulkan run did not finish its 120 frames"; shot novk-stuck; timeout 30 adb shell am force-stop $PKG; fi
if grep -q "not on this device, drawing with OpenGL ES" "$OUT/logcat-novk.txt" && grep -q "I/DokuQuest.*OpenGL ES 3" "$OUT/logcat-novk.txt" \
   && timeout 60 adb pull $DIR/shot-novk.png "$OUT/novk.png" > /dev/null 2>&1; then
  note "no Vulkan 1.3: the game went on with OpenGL ES in a new window and played 120 frames"
else
  err "no Vulkan 1.3: the way to OpenGL ES didn't work"; game_log novk 60
fi

# ---- 3b. the touch controls (input/touch.h) through the emulator's own touch screen: a thumb on the left walks, a
# drag on the right turns the view, a tap on the Bag button opens the bag and Back closes it. The game runs a script
# that only reports where the player stands, every few frames (the touches come from adb, as a finger's would).
timeout 30 adb shell am force-stop $PKG; sleep 2
# (open ground round the player first: the world's spawn can face a cliff)
python3 -c 'import json; print(json.dumps({"steps": [{"wait": 20, "do": "anchor"}, {"do": "fill", "rel": True, "from": [-6, 0, -16], "to": [6, 5, 6], "key": "air"}, {"do": "fill", "rel": True, "from": [-6, -1, -16], "to": [6, -1, 6], "key": "grass"}, {"do": "look", "yaw": 0, "pitch": 0}, {"wait": 2, "do": "report"}] + [{"wait": 4, "do": "report"}] * 70 + [{"wait": 2, "do": "quit"}]}))' > "$OUT/touchrun.json"
timeout 60 adb push "$OUT/touchrun.json" $DIR/touchrun.json > /dev/null || err "could not write the touch script into the game's folder"
adb logcat -c
TW=--world,TestGles; [ $VULKAN_OK = 1 ] && TW=--world,Test
timeout 180 adb shell am start -W -n $PKG/.MainActivity --esa args "--renderer,legacy,$TW,--smoke-frames,3000,--smoke-script,touchrun.json"
up=0
for i in $(seq 1 100); do sleep 3; alive || break; timeout 20 adb logcat -d -v brief 2>/dev/null | grep -q "test report" && { up=1; break; }; done
if [ $up = 1 ]; then
  read -r SW SH < <(timeout 20 adb shell wm size | tr -d '\r' | sed -n 's/.*: *\([0-9]*\)x\([0-9]*\).*/\1 \2/p' | tail -1)
  SW=${SW:-2400}; SH=${SH:-1080}
  [ "$SW" -lt "$SH" ] && { t=$SW; SW=$SH; SH=$t; }   # (the game is landscape)
  note "touch: screen ${SW}x${SH}; walking (left thumb pushed up)"
  # the thumb down, pushed up to the ring's edge, held there 15 s (the emulator draws about a frame a second, and each
  # frame moves the player at most a tenth of a second's walk), then lifted
  X=$((SW * 20 / 100)); Y0=$((SH * 70 / 100)); Y1=$((SH * 57 / 100))
  if timeout 20 adb shell input motionevent DOWN $X $Y0 > /dev/null 2>&1; then
    for y in $(seq $Y0 -20 $Y1) $Y1; do timeout 20 adb shell input motionevent MOVE $X $y > /dev/null 2>&1; done
    sleep 5; shot touch-walking; sleep 10
    timeout 20 adb shell input motionevent UP $X $Y1 > /dev/null 2>&1
  else
    timeout 60 adb shell input swipe $X $Y0 $X $Y1 15000 &
    sleep 5; shot touch-walking; wait
  fi
  sleep 6
  note "touch: turning (a drag on the right)"
  timeout 60 adb shell input swipe $((SW * 55 / 100)) $((SH * 40 / 100)) $((SW * 75 / 100)) $((SH * 40 / 100)) 2500
  sleep 6
  # the game's window can be narrower than the screen (the camera's cut-out beside it, on a side not known here): aim
  # half that difference in from the button's centre, so the tap lands on it whichever side the gap is
  read -r GW GH < <(timeout 20 adb logcat -d -v brief 2>/dev/null | sed -n 's/.*touch: the window is \([0-9]*\)x\([0-9]*\).*/\1 \2/p' | tail -1)
  GW=${GW:-$SW}; GH=${GH:-$SH}
  B=$((GH * 15 / 100)); M=$((GH * 35 / 1000))
  note "touch: the Bag button (game window ${GW}x${GH})"
  timeout 30 adb shell input tap $((GW - M - B * 13 / 10 + (SW - GW) / 2)) $((M + B * 4 / 10))
  sleep 10; shot touch-bag
  timeout 30 adb shell input keyevent KEYCODE_BACK
  sleep 8
fi
for i in $(seq 1 120); do sleep 5; alive || break; done
logs touch
if alive; then err "the touch run did not finish"; shot touch-stuck; timeout 30 adb shell am force-stop $PKG; fi
c=$(crashed touch); [ -n "$c" ] && { err "crash in the touch run"; echo "$c"; }
grep -E "test report" "$OUT/logcat-touch.txt" | sed 's/.*test report: //' | cut -c1-160 > "$OUT/touch-reports.txt"
echo "::group::touch run reports"; cat "$OUT/touch-reports.txt"; echo "::endgroup::"
if [ $up = 1 ]; then
  python3 - "$OUT/touch-reports.txt" <<'PYEOF' || err "the touch controls did not do what they should (see above)"
import math, re, sys
rows = []
for l in open(sys.argv[1]):
    m = re.search(r"screens: (.*?) \| pos (-?[\d.]+) (-?[\d.]+) (-?[\d.]+) yaw (-?\d+)", l)
    if m: rows.append((m.group(1), float(m.group(2)), float(m.group(4)), float(m.group(5))))
ok = True
if len(rows) < 3: print("::error::touch: too few reports (%d)" % len(rows)); sys.exit(1)
x0, z0, yaw0 = rows[0][1], rows[0][2], rows[0][3]
walked = max(math.hypot(r[1] - x0, r[2] - z0) for r in rows)
turned = max(abs(((r[3] - yaw0) + 180) % 360 - 180) for r in rows)
bag = any("inventory" in r[0] for r in rows)
first = next((i for i, r in enumerate(rows) if "inventory" in r[0]), None)
closed = first is not None and any("inventory" not in r[0] for r in rows[first:])
print("::notice::touch: walked %.1f blocks, turned %.0f degrees, bag opened %s, closed again with Back %s" % (walked, turned, bag, closed))
if walked < 0.8: print("::error::touch: the left thumb didn't walk the player"); ok = False
if turned < 10: print("::error::touch: a drag on the right didn't turn the view"); ok = False
if not bag: print("::error::touch: the Bag button didn't open the bag"); ok = False
if bag and not closed: print("::error::touch: Back didn't close the bag"); ok = False
sys.exit(0 if ok else 1)
PYEOF
else
  err "touch: the game never reported (its world didn't open?)"; game_log touch 80
fi

# ---- 4. a start that dies on Vulkan (killed 15 s in, as a crashing driver would): the next start is OpenGL ES
if [ $VULKAN_OK = 1 ]; then
  timeout 30 adb shell am force-stop $PKG; sleep 2
  timeout 60 adb shell am start -n $PKG/.MainActivity > /dev/null; sleep 15
  timeout 30 adb shell am force-stop $PKG; sleep 2
  adb logcat -c
  timeout 60 adb shell am start -n $PKG/.MainActivity > /dev/null; sleep 40
  logs guard
  if grep -q "didn't get through; drawing with OpenGL ES" "$OUT/logcat-guard.txt" && grep -q "I/DokuQuest.*OpenGL ES" "$OUT/logcat-guard.txt"; then
    note "a start killed on Vulkan: the next start drew with OpenGL ES, as it should"
  else
    err "a start killed on Vulkan: the next start did not switch to OpenGL ES"
    grep -E "[IWE]/DokuQuest" "$OUT/logcat-guard.txt" | head -20
  fi
  c=$(crashed guard); [ -n "$c" ] && { err "crash after the Vulkan fallback"; echo "$c"; }
  timeout 30 adb shell am force-stop $PKG
fi
exit $rc
