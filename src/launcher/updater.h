// launcher/updater.h — updates from GitHub Releases (port of launcher/desktop/updater.js's rules).
// The launcher asks GitHub for the newest release of FartGarfunkel/DokuQuest-Rewrite with the system curl
// (curl on Linux, curl.exe on Windows 10+; no HTTP library is linked), picks this platform's build
// (…-linux-x64.tar.gz / …-windows-x64.zip), downloads it into userDir()/versions/, checks its size and
// SHA-256 (the asset's "digest", else the release's SHA256SUMS.txt), unpacks it with the system tar into
// userDir()/versions/<version>/ and makes it the active version. Play then starts that version's game;
// the previous version is kept, so a version whose game fails to start falls back to the one before.
// Worlds are never touched: they live in userDir()/saves.
#pragma once
#include "launcher/process.h"
#include <map>

namespace dq {

struct ReleaseAsset { std::string name, url, digest; i64 size = 0; };
struct ReleaseInfo {
  std::string tag, version, name, body, htmlUrl;
  bool prerelease = false;
  std::vector<ReleaseAsset> assets;
};
bool parseRelease(const json& j, ReleaseInfo& out, std::string& err);   // GitHub /releases/latest JSON
int compareVersions(const std::string& a, const std::string& b);      // semver-ish: 1.0.0-alpha.2 < 1.0.0-beta < 1.0.0
std::string niceVersion(const std::string& v);                        // "0.9.0-alpha.8" → "0.9.0.8" (how players see it)
const ReleaseAsset* pickAsset(const ReleaseInfo& r, const std::string& platform);   // "linux" | "windows"
std::string platformName();
std::string sha256Hex(const void* data, size_t n);
std::string sha256File(const fs::path& p);
bool validRepoSource(const std::string& s);                           // owner/name like updater.js's srcOk

class Updater {
public:
  enum class State { Idle, Checking, DownloadingSums, Downloading, Extracting, Done, Failed };
  std::string source = "FartGarfunkel/DokuQuest-Rewrite";
  std::string apiOverride;          // --update-api URL (tests use a file:// URL to a saved response)

  void check(bool quiet);           // start a check (and the install of a newer version)
  void poll();                      // call every frame
  bool busy() const { return state == State::Checking || state == State::DownloadingSums || state == State::Downloading || state == State::Extracting; }
  State state = State::Idle;
  bool quiet = false;
  std::string message;              // shown under "Check for updates"
  float progress = 0;               // download 0..1
  std::string installed;            // set when a new version was installed by the last check
  std::string latest;               // newest version GitHub knows about

  // installed versions (userDir()/versions/versions.json)
  std::string activeVersion() const;
  // the game (or launcher) executable to run: the newest good installed version if it is newer than this
  // build, else the one next to the launcher. *version receives the version it belongs to.
  fs::path executable(const std::string& base, std::string* version) const;
  void markBad(const std::string& version, const std::string& why);
  std::string currentVersion() const;   // max(this build, active installed)
private:
  void fail(const std::string& msg);
  void startDownload();
  void finishDownload();
  void finishExtract();
  bool spawnCurl(const std::string& url, const fs::path& out, bool api);
  json loadState() const;
  void saveState(const json& j) const;
  fs::path tmpDir() const;
  ChildProcess proc;
  ReleaseInfo rel;
  ReleaseAsset asset;
  std::string wantSha;
};

} // namespace dq
