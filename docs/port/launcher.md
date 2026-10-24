# Launcher, updater, Mod Studio, packaging — port notes

Sources: `launcher/desktop/launcher.html` (+ `main.js`, `preload.js`, `updater.js`, `lan.js`), `launcher/news.html`,
`launcher/icons/`, `tools/package.sh`, `tools/github/release.yml`, the HTML Mod Studio (`dokuquest-mod-studio.html`).

## What was ported (src/launcher/)
| File | What |
|---|---|
| `launcher_main.cpp` | window + GL context (tries 4.6 → 3.3 core; its `GL_RENDERER` feeds the graphics suggestion), fonts like the game (Pixelify Sans 13/16/20/26, Jersey 15 34, ProggyClean for code), frame loop, tabs, smoke/test options, hand-off to a newer installed launcher |
| `ui.cpp` | the look from launcher.html's CSS: dirt and button textures from the same seeded mulberry32 noise, `.mcbtn` grey/green buttons (hover blue + yellow text), dark `.news/.side` panels, Mod Studio nav buttons |
| `news.cpp` | news panel: renders `assets/news/*.dqn` (simple rich text, see below), virtualised, scrolls with PageUp/Down and the right stick |
| `home.cpp` | side panel (your worlds = quick play, tips), bottom bar (logo, version, notice, player name, Options… / Game folder, Play / Dedicated Server), Play: starts `dokuquest --launcher --renderer legacy\|modern [--world FOLDER] [--userdir DIR]` as a child, shows "Running…", comes back (raises the window, reloads settings.json) when it exits |
| `process.*` | child processes: `posix_spawn(p)` (stdout/stderr redirection, chdir with glibc ≥ 2.29) / `CreateProcessW` (quoted command line, hidden window for curl/tar) |
| `options.cpp` | renderer (Modern 4.6 default, Legacy 3.3, Vulkan shown disabled), presets potato…ultra/custom via `Settings::applyPreset`, the GPU suggestion + "Use the suggestion", render distance, distant terrain, micro-voxel detail and distance, water, shadows, particles, leaves, clouds, frame-rate limit (original choices), full screen, resource/shader pack pickers (`userDir()/resourcepacks`, `shaderpacks`), keep-launcher-open, updates |
| `server_tab.cpp` | in-process `DedicatedServer`: choose/create a server world (`userDir()/servers/`), copy a single-player world into it, port, max players, MOTD, mode, difficulty, password, LAN, PvP, view distance (saved in `servers/server.json`), start/stop, live console (`logAddSink`, colour by level, history), command input, players, TPS/mspt/uptime |
| `modstudio*.cpp` | Mod Studio: Overview, DQ API (updated for JS mods, data packs, native plugins, shader packs, resource packs), Mod editor (templates incl. a new item+recipe one, QuickJS compile-only syntax check of `(function (DQ) {"use strict"; … })` with line numbers, save to `mods/<id>/main.js + mod.json`, copy), Installed mods (enable/disable via `ModLoader`, load in editor, open folder), Data packs (forms writing `data/blocks.json`, `items.json`, `recipes.json` incl. shaped/shapeless/smelting, validated against the registry), Engine source (areas from CLAUDE.md, open on GitHub, local source browser when a checkout is next to the build, build instructions + the original safety checklist) |
| `updater.*` | updater.js's rules on GitHub Releases (see below), SHA-256, version compare |
| `dokuquest.rc` | Windows icon resource for both executables (`assets/icon.ico`) |

Other files: `assets/icon.png` (icon-256), `assets/icon.ico`, `assets/news/news.dqn` (C++ entries), `assets/news/alpha.dqn`
(the 103 HTML-alpha entries, converted by `tools/port/news2dqn.py`), `docs/CHANGELOG.md`, `tools/package.sh`,
`.github/workflows/ci.yml`, `.github/workflows/release.yml`, `tools/test/github-release-latest.json` (saved sample response).

## News format (`assets/news/*.dqn`)
`## Title | Date` starts an entry, `- ` is a bullet, other lines are paragraphs, inline `**bold**`, `*italic*`, `` `code` ``,
`# ` lines are comments. `news.dqn` is shown first, then the other files by name. The launcher reads the notes of the
version Play would run (an installed update brings its own). Rules enforced by the converter AND the loader: no line may
mention Herobrine, and every entry ends with "Removed Herobrine.". Glyphs the pixel font lacks (→ ▲ ▼ ☺ ⚔ 🛡 ✦) are
replaced by the converter.

## Updater
- `curl` (Linux) / `curl.exe` (Windows 10+) child process → `https://api.github.com/repos/<source>/releases/latest`
  (default source `FartGarfunkel/DokuQuest-Rewrite`, changeable in Options, saved as `settings.extra.updateSource`).
  Messages follow updater.js (offline, 404, 401, 403/429 rate limit, size, incomplete, checksum).
- Picks `…-linux-x64.tar.gz` / `…-windows-x64.zip`, refuses > 1 GiB, verifies the size and SHA-256 (the asset's
  `digest` field, else `SHA256SUMS.txt` / `<asset>.sha256` in the release), unpacks with the system `tar`
  into `userDir()/versions/<ver>/DokuQuest/`, records it in `versions/versions.json` (`active`, `previous`, `installed`, `bad`)
  and deletes older versions. Play runs the active version's game if it is newer than the launcher's own build.
- Fallback: if a downloaded version's game exits non-zero within 15 s it is marked bad and the previous one (or the bundled
  one) is used again. At start a launcher hands off to a newer installed launcher (`--no-handoff` is passed on); if that
  one exits non-zero within 4 s the version is marked bad and this launcher carries on.
- Auto-check at start when `Settings::autoUpdate` (not during smoke runs); "Check for updates" button in Options.
- Tested with a scratch test (version compare, SHA-256 vectors, JSON parsing of the saved sample, QuickJS check, GPU
  suggestions) and full installs from `file://` URLs (good digest → installed + active; wrong digest → rejected).
  The real API answered from this sandbox: the repo's latest release is still the old Rust `v0.2.3`, which is lower than
  `1.0.0-alpha.1`, so nothing is offered.

## Packaging and CI
- `cmake --install build --prefix X` = one folder with `dokuquest`, `dokuquest-launcher`, `dokuquest-server`, `data/`, `assets/`.
- `DQ_PORTABLE=ON` (release builds) links libstdc++/libgcc statically on Linux.
- `tools/package.sh <build> <version> [linux|windows]` → `dist/DokuQuest-<ver>-x86_64.AppImage` (AppRun starts the launcher;
  `--server` / `--game` run the others; bundles non-system libs found by ldd but never libGL/libEGL/libvulkan or other
  driver/desktop libs; appimagetool + type2 runtime from their GitHub releases, cached in `.cache/`),
  `…-linux-x64.tar.gz`, `…-windows-x64.zip` (7z or PowerShell). Tested locally: the AppImage runs the launcher.
- `ci.yml`: every push/PR — Linux (ubuntu-22.04, X11/Wayland/GL/EGL/ALSA/PulseAudio dev packages) and Windows (MSVC)
  builds after `tools/fetch_deps.sh` (Git Bash on Windows); Linux smoke test under xvfb + Mesa: create a world and run
  300 frames, open launcher tabs, run the server and stop it; fails on a crash or any `[error]` line; screenshots uploaded.
- `release.yml`: ONLY a pushed `v*` tag or a manual run (version input, draft by default). Takes the notes from the
  `## <version>` section of `docs/CHANGELOG.md` (fails if missing or if it names Herobrine; makes it end with
  "Removed Herobrine."), builds + smoke-tests Linux, builds Windows, packages, writes `SHA256SUMS.txt`, and publishes with
  `gh release create … --latest` (a full release, because the updater reads `/releases/latest`).

## Test options (launcher)
`--userdir DIR --smoke-frames N --smoke-shot FILE --tab news|server|studio[:overview|api|editor|mods|data|engine]|options`,
`--update-api URL` (e.g. `file:///…/latest.json`), `--check-updates`, `--start-server`, `--play`, `--no-handoff`.

## Differences from the original
- Options is a tab instead of a dialog, and has more (renderer, presets, packs). The Dedicated Server and the Mod Studio
  are tabs of the launcher (they were the game's server screen and a separate HTML page).
- Settings are the game's `settings.json`; the "keep the launcher open" option is `settings.extra.launcherKeepOpen`.
  While the game runs, the game's options are locked (the game saves settings.json when it quits).
- Updates replace the whole program version (game + launcher) instead of the single dokuquest.html; no "Install update…"
  file picker and no private-repo token (curl would need it on its command line).
- Mod Studio saves mods straight into the game folder instead of downloading .js files / localStorage.

## What's left
- Server tab: networking (player list, kick/ban, LAN broadcast and the password) arrives with the network workstream;
  the tab already ticks and saves the world. Game mode/difficulty only apply when the world is created.
- Native plugin API docs in the studio describe the plan (`mod/` header not published yet).
- Profile/skin picker in the launcher (the game owns the character creator for now).
- Windows build and the release workflow are untested here (no Windows/Actions runner); the Windows code paths
  (CreateProcessW, curl.exe/tar.exe, .rc icon, 7z zip) need a first CI run.
- The updater cannot update a running AppImage in place; new versions go to `userDir()/versions/` instead (the AppImage
  launcher hands off to them).
