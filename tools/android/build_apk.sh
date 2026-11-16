#!/usr/bin/env bash
# tools/android/build_apk.sh — DokuQuest for Android, as one APK for 64-bit ARM phones (arm64-v8a):
#   the game as libmain.so (CMake + the Android NDK; CMakeLists.txt, ANDROID), SDL's Java activity with
#   android/java (javac + d8), the manifest, resources and launcher icons from android/ (aapt2), and the game's files
#   (data.dqa, data/resourcepacks, data/sounds, assets/) that client/android_boot.cpp unpacks on the first start.
#
#   bash tools/android/build_apk.sh          → dist/android/DokuQuest-<version>-android-arm64.apk
#   ABI=x86_64 bash tools/android/build_apk.sh   → ...-android-x86_64.apk (for the emulator test in CI)
#
# Needs: the Android SDK (ANDROID_HOME, with build-tools and a platform) and an NDK (ANDROID_NDK_HOME, or one in
# $ANDROID_HOME/ndk/), cmake, ninja, a JDK 17+, zip, git.
# Signing: ANDROID_KEYSTORE (+ ANDROID_KEYSTORE_PASS, ANDROID_KEY_ALIAS) if set, else android/debug.keystore (password
# "android", alias "dokuquest"). Keep using the same key: Android installs a new build over an old one (and keeps its
# saves) only when both are signed with the same key.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"
ABI="${ABI:-arm64-v8a}"
case "$ABI" in arm64-v8a) ARCH=arm64 ;; x86_64) ARCH=x86_64 ;; *) echo "error: ABI is arm64-v8a or x86_64"; exit 1 ;; esac
OUT="$ROOT/dist/android"
WORK="$ROOT/build-android-apk-$ARCH"
BUILD="${ANDROID_BUILD_DIR:-$ROOT/build-android-$ARCH}"
MIN_SDK=26      # Android 8: adaptive icons; the game itself needs Vulkan 1.3 (Android 13+) and says so when it's missing
TARGET_SDK=33   # (SDL 2.30's Java registers its USB receiver without the flag Android 14 asks of apps targeting 34+)

SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
[ -n "$SDK" ] && [ -d "$SDK" ] || { echo "error: set ANDROID_HOME to the Android SDK"; exit 1; }
# the NDK: the one named, else the newest installed. SDL 2 compiles the NDK's cpufeatures, so it must have it.
pick_ndk() { for d in "$@"; do if [ -n "$d" ] && [ -f "$d/build/cmake/android.toolchain.cmake" ] && [ -f "$d/sources/android/cpufeatures/cpu-features.c" ]; then echo "$d"; return; fi; done; }
mapfile -t NDKS < <(ls -d "$SDK"/ndk/* 2>/dev/null | sort -rV)
NDK="$(pick_ndk "${ANDROID_NDK_HOME:-}" "${ANDROID_NDK_ROOT:-}" "${NDKS[@]}" "${ANDROID_NDK_LATEST_HOME:-}")"
[ -n "$NDK" ] || { echo "error: no Android NDK with sources/android/cpufeatures (install one: sdkmanager 'ndk;27.2.12479018')"; exit 1; }
BT="$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)"
PLATFORM="$(ls -d "$SDK"/platforms/android-* | grep -E '/android-[0-9]+$' | sort -V | tail -1)"
ANDROID_JAR="$PLATFORM/android.jar"
LLVM="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
for f in "$BT/aapt2" "$BT/d8" "$BT/zipalign" "$BT/apksigner" "$ANDROID_JAR" "$LLVM/llvm-strip"; do
  [ -e "$f" ] || { echo "error: missing $f"; exit 1; }
done

VERSION="$(sed -n 's/^set(DQ_VERSION "\([^"]*\)".*/\1/p' CMakeLists.txt)"
# minutes since 2026-01-01: every build counts higher than the one before, so it installs over it
VERSION_CODE="${VERSION_CODE:-$(( ($(date -u +%s) - 1767225600) / 60 ))}"
SHA="$(git rev-parse --short HEAD 2>/dev/null || echo local)"
BUILD_NAME="DokuQuest $VERSION build $VERSION_CODE ($SHA)"
echo "$BUILD_NAME"
echo "  NDK $(basename "$NDK"), build-tools $(basename "$BT"), $(basename "$PLATFORM"), min SDK $MIN_SDK, target SDK $TARGET_SDK"

# ---- the game: libmain.so
echo "== libmain.so"
mkdir -p "$OUT"
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-$MIN_SDK -DANDROID_STL=c++_static > "$OUT/cmake.log" 2>&1 \
  || { tail -60 "$OUT/cmake.log"; echo "error: cmake configure failed"; exit 1; }
cmake --build "$BUILD" --target main -- -k 0
rm -rf "$WORK"; mkdir -p "$WORK/apk/lib/$ABI" "$OUT"
SO="$WORK/apk/lib/$ABI/libmain.so"
"$LLVM/llvm-strip" --strip-unneeded -o "$SO" "$BUILD/libmain.so"
echo "  $(du -h "$BUILD/libmain.so" | cut -f1) built, $(du -h "$SO" | cut -f1) stripped"

# What Android checks when it loads the library: SDL's Java side has to find these, and every library it names must
# be one Android gives apps (anything else, like libc++_shared.so, would have to be packed in as well).
for sym in SDL_main JNI_OnLoad Java_org_libsdl_app_SDLActivity_nativeSetupJNI Java_org_libsdl_app_SDLActivity_nativeRunMain \
           Java_org_libsdl_app_SDLActivity_onNativeSurfaceCreated Java_org_libsdl_app_SDLControllerManager_nativeSetupJNI \
           Java_org_libsdl_app_SDLControllerManager_onNativePadDown Java_org_libsdl_app_SDLAudioManager_nativeSetupJNI; do
  "$LLVM/llvm-nm" -D --defined-only "$SO" | awk '{print $3}' | grep -qx "$sym" || { echo "error: libmain.so does not export $sym"; exit 1; }
done
PUBLIC_LIBS=" libandroid.so libc.so libdl.so libEGL.so libGLESv1_CM.so libGLESv2.so libGLESv3.so libjnigraphics.so liblog.so libm.so libOpenSLES.so libvulkan.so libz.so libaaudio.so libnativewindow.so libmediandk.so libcamera2ndk.so libamidi.so libsync.so "
NEEDED="$("$LLVM/llvm-readelf" -d "$SO" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')"
echo "  needs: $(echo $NEEDED)"
for lib in $NEEDED; do
  case "$PUBLIC_LIBS" in *" $lib "*) ;; *) echo "error: libmain.so needs $lib, which Android doesn't give apps"; exit 1 ;; esac
done
"$LLVM/llvm-readelf" -lW "$SO" | awk '$1 == "LOAD" && $NF != "0x4000" && $NF != "0x10000" {bad=1} END {exit bad}' \
  || echo "  note: libmain.so's segments are not 16 KB aligned"

# ---- SDL's Java side + our activity
echo "== java"
mkdir -p "$WORK/classes" "$WORK/dex"
mapfile -t JAVA_SRC < <(find third_party/SDL2/android-project/app/src/main/java android/java -name '*.java' | sort)
javac -encoding UTF-8 -source 8 -target 8 -Xlint:-options -nowarn -bootclasspath "$ANDROID_JAR" -d "$WORK/classes" "${JAVA_SRC[@]}"
mapfile -t CLASSES < <(find "$WORK/classes" -name '*.class' | sort)
"$BT/d8" --release --min-api $MIN_SDK --lib "$ANDROID_JAR" --output "$WORK/dex" "${CLASSES[@]}"
cp "$WORK/dex/classes.dex" "$WORK/apk/"

# ---- manifest, resources, the launcher icon
echo "== resources"
"$BT/aapt2" compile --dir android/res -o "$WORK/res.zip"
"$BT/aapt2" link -o "$WORK/game.apk" -I "$ANDROID_JAR" --manifest android/AndroidManifest.xml \
  --min-sdk-version $MIN_SDK --target-sdk-version $TARGET_SDK --version-code "$VERSION_CODE" --version-name "$VERSION" \
  "$WORK/res.zip"

# ---- the game's files, as a release has them (tools/package.sh): data/ in one archive, data.dqa, except
# resourcepacks/ and sounds/, which stay folders; then assets/. files.txt lists them for client/android_boot.cpp.
echo "== game files"
STAGE="$WORK/apk/assets/dq"
mkdir -p "$STAGE/data"
( cd data && mapfile -t PARTS < <(ls -A | grep -v -x -e resourcepacks -e sounds) && cmake -E tar cf "$STAGE/data.dqa" --format=zip "${PARTS[@]}" )
for d in resourcepacks sounds; do if [ -d "data/$d" ]; then cp -a "data/$d" "$STAGE/data/"; fi; done
cp -a assets "$STAGE/assets"
{ echo "$BUILD_NAME"; ( cd "$STAGE" && find . -type f | sed 's|^\./||' | LC_ALL=C sort ); } > "$WORK/files.txt"
mv "$WORK/files.txt" "$STAGE/files.txt"
echo "  $(( $(wc -l < "$STAGE/files.txt") - 1 )) files, $(du -sh "$STAGE" | cut -f1) (data.dqa $(du -h "$STAGE/data.dqa" | cut -f1))"

# ---- the APK: code, library, files; aligned and signed
echo "== apk"
cp "$WORK/game.apk" "$WORK/unaligned.apk"
( cd "$WORK/apk" && zip -q -X -r "$WORK/unaligned.apk" classes.dex lib \
  && zip -q -X -r -n .png:.dqa:.ogg:.jpg "$WORK/unaligned.apk" assets )
"$BT/zipalign" -f -p 4 "$WORK/unaligned.apk" "$WORK/aligned.apk"
KS="${ANDROID_KEYSTORE:-android/debug.keystore}"
PASS="${ANDROID_KEYSTORE_PASS:-android}"
ALIAS="${ANDROID_KEY_ALIAS:-dokuquest}"
APK="$OUT/DokuQuest-$VERSION-android-$ARCH.apk"
rm -f "$APK"
"$BT/apksigner" sign --ks "$KS" --ks-pass "pass:$PASS" --key-pass "pass:$PASS" --ks-key-alias "$ALIAS" --out "$APK" "$WORK/aligned.apk"
"$BT/apksigner" verify --verbose "$APK" | grep -E "^Verifie|^Verified|DOES NOT|ERROR" || true
"$BT/aapt2" dump badging "$APK" | grep -E "^(package|sdkVersion|targetSdkVersion|application-label:|application-icon-640|launchable-activity|native-code)" || true
echo "$BUILD_NAME" > "$OUT/build-$ARCH.txt"
echo "APK: $APK ($(du -h "$APK" | cut -f1))"
