# DokuQuest on Android

One APK for 64-bit ARM phones (arm64-v8a), built by GitHub Actions (`.github/workflows/android.yml`) on every push
to the `android` branch, and published as the pre-release **android-latest** (Releases page). A pre-release is never
GitHub's "latest" release, so the desktop launcher's updater never sees it.

## Install
- On the phone, open the release page, download `DokuQuest-<version>-android-arm64.apk` and open it (allow installing
  from the browser / Files when Android asks).
- An APK signed with another key (an older test build made elsewhere) must be uninstalled first. Builds from this
  workflow are all signed with the same key (`android/debug.keystore`, or a key in the repository secrets
  `ANDROID_KEYSTORE_B64`, `ANDROID_KEYSTORE_PASS`, `ANDROID_KEY_ALIAS`), so each installs over the last and keeps the
  worlds.
- The first start of each build unpacks the game's files (a few seconds of black screen).

## Which phones
- **Vulkan 1.3** draws the game (Android 13+ on a recent phone: Pixel 8/9/10, Galaxy S23+, ...): the Vulkan renderer
  (`render/vk_backend.cpp`) running the game's OpenGL calls.
- **OpenGL ES 3** where Vulkan 1.3 isn't there (Android 12 and older, e.g. a Galaxy Note 10+): the Legacy renderer on
  the phone's GLES 3.2/3.1/3.0 driver (`render/gl.cpp`, "OpenGL ES"). Options → Video → Renderer switches between the
  two for the next start (OpenGL ES is also the way out if a phone's Vulkan driver misbehaves).
- `DQ_GLES=1 ./build/dokuquest --renderer legacy` runs the GLES path on a PC (Mesa has GLES) for testing.

## Playing
- Touch (`input/touch.h`): a fixed move stick bottom left (push past its ring, or tap Sprint, to sprint); drag anywhere
  else to look. Aiming (Options → Controls → Touch → Touch Aiming): "Where You Touch" (default: tap a block to use it or
  place on it, tap a monster to hit it, hold on a block to mine it) or "Crosshair" (taps and holds act on the centre).
  Buttons: Jump, Mine, Use (hold: eat, draw a bow), Sneak (a toggle), Cast (the quick spell), Dodge; by the hotbar Drop
  (held: the stack) and Swap hands; along the top Pause, Bag, More, Map, Journal, Spells, Chat. More opens a panel with
  every other action (skills, spellbook, next spell, quick spell, companions, crawl, placing mode, next hotbar row,
  camera, zoom, pick block, drop stack, command, photo mode, screenshot, hide HUD). Tap the hotbar to pick a slot.
- In the bag: tap = take/put (left click), hold still = half a stack / one (right click; on a crafting result: make
  all), double tap = quick move (shift click), drag from a slot = carry it to where you let go (off the panel: thrown),
  drag while holding something = spread it. The item's tooltip shows above the finger. In menus a finger is the
  pointer, and dragging a list up or down scrolls it.
- Options → Controls → Touch: Touch Controls Auto (hidden while a controller is connected) / On / Off, Touch Aiming,
  Auto-Jump (on from a phone's first start), Look Speed, Button Size, Button Opacity. GUI Scale "Auto" makes the
  interface as large as the 1280x720 layout allows on a phone (150% on 2400x1080; `ui/theme.cpp computeZoom`).
- `DQ_TOUCH=1` shows the touch controls on a PC; `tools/playtests/touch.json` and `touch_inventory.json` drive them with
  fingers (the `touch` and `tbutton` script steps).
- A controller (Bluetooth or USB) plays everything, menus included; using one hides the touch buttons (a touch
  brings them back).
- Back button / back gesture = Escape. Text fields open the on-screen keyboard.
- The first start uses the Low preset; Options → Video raises it.
- Leaving the app saves the world (Android may close a game in the background without asking).

## Folders
- Game files: the app's own storage (`/data/data/org.dokuquest.app/files/game`), unpacked from the APK's
  `assets/dq/` (`files.txt` lists them; `client/android_boot.cpp`).
- Player folder (userDir: worlds, settings.json, resourcepacks/, mods/, screenshots/, latest.log):
  `Android/data/org.dokuquest.app/files` on the phone's storage. A computer reaches it over USB (MTP), so resource
  packs and worlds can be copied in and out.
- Logs: `latest.log` there, and logcat: `adb logcat -s DokuQuest SDL`.

## How it's built (`tools/android/build_apk.sh`)
- CMake with the NDK (`ANDROID`): the whole game, SDL included, is `libmain.so` (`src/client/main.cpp` becomes
  `SDL_main`). Linked with `--no-undefined`, so a missing system library is a build error instead of
  "dlopen failed: cannot locate symbol" on the phone; the script also checks every library it names is one Android
  gives apps, and that SDL's Java entry points are exported.
- Java: SDL's activity (`third_party/SDL2/android-project/.../org/libsdl/app`) + `android/java/.../MainActivity.java`
  (javac against android.jar + the build-tools' lambda stubs, then d8).
- Resources: `android/AndroidManifest.xml`, `android/res` (the launcher icon is made from `assets/icon.png` by
  `tools/android/make_icons.py`: run it again after changing the icon and commit the PNGs).
- The game's files as a release has them (`tools/package.sh`): `data.dqa` + `data/resourcepacks`, `data/sounds`,
  `assets/`.
- Aligned and signed (APK signature v2 + v3). Min SDK 26, target SDK 33.

Building it on a PC (Linux): install the Android command-line tools, then
`sdkmanager "platforms;android-35" "build-tools;35.0.0" "ndk;27.2.12479018"`, set `ANDROID_HOME`, and run
`bash tools/android/build_apk.sh` (needs cmake, ninja, a JDK 17+, zip). `ABI=x86_64` builds for an emulator.

## The CI's emulator test (`tools/android/emulator_test.sh`)
The x86_64 build in an Android 14 emulator with software Vulkan: install, first start (unpacking, title screen),
Home and back (a new Vulkan surface), a new world for 300 frames on Vulkan, then the same world reopened on
OpenGL ES for 200 frames, the touch controls through the emulator's touch screen (adb swipes and taps: walk, turn,
the Bag button, Back), and a start killed on Vulkan (the next one uses OpenGL ES). Its notices/errors say what happened; screenshots are printed into the job log as base64
JPEGs between `=== screenshot <name>` and `=== end`.

## Mobile GPU notes (kept in the code)
- No "scaled" vertex formats (16/8-bit integers read as floats): phone GPUs leave them out. Such inputs are integer
  inputs made float in the shader (voxel.glsl, distant.glsl, particles.glsl, clouds.glsl).
- Vulkan swapchain: upright images (`preTransform` identity) on a turned screen, whose SUBOPTIMAL is expected; a new
  surface when the app comes back (`gfxVulkanSurfaceChanged`).
- ImGui's GL backend stays on glad on Android (it would switch itself to the system's GLES 3 around the Vulkan
  renderer): `DQ_IMGUI_GL_VIA_GLAD`, `third_party/imgui/backends/imgui_impl_opengl3.h`.
