// client/android_boot.cpp — see android_boot.h. Only built into the Android game (empty elsewhere).
#ifdef __ANDROID__
#include "client/android_boot.h"
#include "common/fs.h"
#include <SDL.h>
#include <SDL_system.h>
#include <cstdio>
#include <fstream>
#include <vector>

namespace dq {

namespace {

// a whole file from inside the APK (SDL reads relative paths from the APK's assets/)
bool readApkFile(const char* name, std::string& out) {
  SDL_RWops* in = SDL_RWFromFile(name, "rb");
  if (!in) return false;
  out.clear();
  std::vector<char> buf(1 << 16);
  size_t n;
  while ((n = SDL_RWread(in, buf.data(), 1, buf.size())) > 0) out.append(buf.data(), n);
  SDL_RWclose(in);
  return true;
}

// one file from the APK's assets/ to a file on the device (through a .part file, so a cut-off copy is never taken
// for a whole one)
bool copyApkFile(const std::string& name, const fs::path& to) {
  SDL_RWops* in = SDL_RWFromFile(name.c_str(), "rb");
  if (!in) { DQ_ERR("android: %s is not in the APK (%s)", name.c_str(), SDL_GetError()); return false; }
  std::error_code ec;
  fs::create_directories(to.parent_path(), ec);
  fs::path part = to; part += ".part";
  FILE* out = std::fopen(part.c_str(), "wb");
  if (!out) { SDL_RWclose(in); DQ_ERR("android: can't write %s", part.c_str()); return false; }
  std::vector<char> buf(1 << 20);
  bool ok = true;
  size_t n;
  while ((n = SDL_RWread(in, buf.data(), 1, buf.size())) > 0)
    if (std::fwrite(buf.data(), 1, n, out) != n) { ok = false; break; }
  SDL_RWclose(in);
  if (std::fclose(out) != 0) ok = false;
  if (ok) { fs::rename(part, to, ec); ok = !ec; }
  if (!ok) { fs::remove(part, ec); DQ_ERR("android: copying %s failed", name.c_str()); }
  return ok;
}

std::string firstLine(const std::string& s) { return s.substr(0, s.find('\n')); }

} // namespace

bool androidPrepare(std::string& error) {
  // the Back button (and the back gesture) reaches the game as a key instead of closing it: App turns it into Escape
  SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  // SDL would offer the phone's tilt sensor as a joystick; controllers are the real ones only
  SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");

  const char* internal = SDL_AndroidGetInternalStoragePath();
  if (!internal) { error = std::string("The app's storage can't be opened: ") + SDL_GetError(); return false; }
  const fs::path game = fs::path(internal) / "game";

  // The list of the game's files in the APK (tools/android/build_apk.sh writes it): the first line names the build,
  // then one path per line, relative to the APK's assets/dq/.
  std::string list;
  if (!readApkFile("dq/files.txt", list) || list.empty()) { error = "The APK has no game files in it (assets/dq/files.txt). Reinstall DokuQuest."; return false; }
  const std::string build = firstLine(list);
  std::string have;
  {
    std::ifstream f(game / ".build");
    std::getline(f, have);
  }
  if (have != build) {
    DQ_LOG("android: unpacking the game's files for %s", build.c_str());
    const double t0 = nowSeconds();
    std::error_code ec;
    fs::remove_all(game, ec);   // only the game's own files live here (the player's are in userDir); a file a newer
                                // build dropped must not stay behind, as loose data/ files win over data.dqa
    fs::create_directories(game, ec);
    size_t at = list.find('\n'), files = 0;
    while (at != std::string::npos) {
      size_t end = list.find('\n', at + 1);
      std::string rel = list.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
      at = end;
      if (!rel.empty() && rel.back() == '\r') rel.pop_back();
      if (rel.empty()) continue;
      if (!copyApkFile("dq/" + rel, game / fs::path(rel))) { error = "Unpacking the game's files failed (" + rel + "). Is the phone's storage full?"; return false; }
      files++;
    }
    std::ofstream(game / ".build") << build << "\n";
    DQ_LOG("android: %zu files unpacked in %.1f s", files, nowSeconds() - t0);
  }
  setInstallDirOverride(game);

  // the player's folder: Android/data/org.dokuquest.app/files on the shared storage (a computer reaches it over USB, so
  // resource packs and saves can be copied in and out), or the app's own storage when the shared one isn't there
  fs::path user;
  if (SDL_AndroidGetExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_WRITE)
    if (const char* ext = SDL_AndroidGetExternalStoragePath()) user = ext;
  if (user.empty()) user = fs::path(internal) / "user";
  std::error_code ec;
  fs::create_directories(user, ec);
  setUserDirOverride(user);
  DQ_LOG("android: game files in %s, player folder %s", game.c_str(), user.c_str());
  return true;
}

} // namespace dq
#endif
