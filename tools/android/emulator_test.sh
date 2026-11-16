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
note() { echo "::notice::emulator: $*"; }
err() { echo "::error::emulator: $*"; rc=1; }
alive() { [ -n "$(adb shell pidof $PKG 2>/dev/null | tr -d '\r')" ]; }
shot() {   # a screenshot of the screen as it is now, into the log
  adb exec-out screencap -p > "$OUT/$1.png" 2>/dev/null || return
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
logs() { adb logcat -d -v brief > "$OUT/logcat-$1.txt" 2>/dev/null; }
crashed() {   # anything in logcat that means the app died
  grep -E "FATAL EXCEPTION|Fatal signal|UnsatisfiedLinkError|dlopen failed|SIGSEGV|SIGABRT|Abort message|E/DokuQuest.*(crashed|uncaught)" "$OUT/logcat-$1.txt" | grep -v "^--------- " | head -30
}
game_log() { grep -E "DokuQuest|SDL|vulkan|Vulkan" "$OUT/logcat-$1.txt" | grep -E "[IWEF]/(DokuQuest|SDL)" | head -${2:-80}; }

echo "Android $(adb shell getprop ro.build.version.release | tr -d '\r'), ABIs $(adb shell getprop ro.product.cpu.abilist | tr -d '\r'), Vulkan HAL $(adb shell getprop ro.hardware.vulkan | tr -d '\r')"
adb install -r "$APK" || { err "the APK did not install"; exit 1; }
note "installed $(basename "$APK")"

# ---- 1. first start: unpacking, Vulkan, the title screen
adb logcat -c
t0=$(date +%s)
adb shell am start -W -n $PKG/.MainActivity
state=""
for i in $(seq 1 60); do
  sleep 5
  logs first
  if grep -qE "I/DokuQuest.*Vulkan 1\.3 \(" "$OUT/logcat-first.txt"; then state=vulkan; break; fi
  if grep -qE "E/DokuQuest|FATAL EXCEPTION|Fatal signal|dlopen failed" "$OUT/logcat-first.txt"; then state=error; sleep 5; break; fi
  if ! alive && [ $i -gt 2 ]; then state=gone; break; fi
done
sleep 20   # the title screen draws
logs first
echo "::group::game log, first start"; game_log first 120; echo "::endgroup::"
grep -E "I/DokuQuest.*(unpack|files unpacked|game files in|Vulkan 1\.3 \(|swapchain)" "$OUT/logcat-first.txt" | sed 's/^/  /' | while IFS= read -r l; do note "${l:0:300}"; done
grep -E "E/DokuQuest" "$OUT/logcat-first.txt" | sort -u | head -20 | while IFS= read -r l; do echo "::warning::emulator: ${l:0:300}"; done
c=$(crashed first); [ -n "$c" ] && { err "crash on the first start"; echo "$c"; }
alive && note "still running after $(( $(date +%s) - t0 )) s (state: ${state:-waiting})" || err "the game is not running after the first start (state: ${state:-none})"
shot title
VULKAN_OK=0; [ "$state" = vulkan ] && VULKAN_OK=1
adb shell am force-stop $PKG
sleep 2

# ---- 2. a world at low settings (the emulator draws with a software GPU), 300 frames, a screenshot
if [ $VULKAN_OK = 1 ]; then
  echo '{"preset":"custom","renderer":"vulkan","renderDist":4,"farTerrain":256,"shadows":0,"waterQuality":0,"clouds":false,"portalViews":false,"scale":0.5,"particles":1,"weatherParticles":0,"microVoxels":0,"plantDetail":"flat","shaderLevel":0,"fancyLeaves":false,"volumetricClouds":false}' > "$OUT/settings.json"
  adb push "$OUT/settings.json" $DIR/settings.json > /dev/null || err "could not write settings.json into the game's folder"
  adb shell rm -f $DIR/shot.png
  adb logcat -c
  t0=$(date +%s)
  adb shell am start -W -n $PKG/.MainActivity --esa args "--create,Test,--seed,hello,--smoke-frames,300,--smoke-shot,$DIR/shot.png"
  for i in $(seq 1 240); do sleep 5; alive || break; [ $((i % 24)) = 0 ] && shot "world-$((i / 12))min"; done
  logs world
  secs=$(( $(date +%s) - t0 ))
  echo "::group::game log, world"; game_log world 200; echo "::endgroup::"
  c=$(crashed world); [ -n "$c" ] && { err "crash in the world run"; echo "$c"; }
  if alive; then err "the world run did not finish its 300 frames in $secs s"; shot world-stuck; adb shell am force-stop $PKG; fi
  adb pull $DIR/latest.log "$OUT/latest.log" > /dev/null 2>&1 && { echo "::group::latest.log"; tail -150 "$OUT/latest.log"; echo "::endgroup::"; }
  grep -E "^\[error\]" "$OUT/latest.log" 2>/dev/null | sort -u | head -20 | while IFS= read -r l; do echo "::warning::emulator latest.log: ${l:0:300}"; done
  if adb pull $DIR/shot.png "$OUT/world.png" > /dev/null 2>&1; then
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
exit $rc
