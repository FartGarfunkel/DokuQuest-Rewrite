#include "launcher/updater.h"
#include <fstream>
#include <cstring>
#include <cctype>

namespace dq {

static constexpr i64 APP_MAX = 1ll << 30;   // updater.js: APP_MAX

// ---------------------------------------------------------------- versions
static std::vector<std::string> splitDots(const std::string& s) { return splitStr(s, '.'); }
static bool isNum(const std::string& s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }); }

int compareVersions(const std::string& a0, const std::string& b0) {
  std::string a = a0, b = b0;
  if (!a.empty() && (a[0] == 'v' || a[0] == 'V')) a.erase(0, 1);
  if (!b.empty() && (b[0] == 'v' || b[0] == 'V')) b.erase(0, 1);
  auto cut = [](const std::string& s, std::string& main, std::string& pre) { size_t d = s.find('-'); main = s.substr(0, d); pre = d == std::string::npos ? "" : s.substr(d + 1); };
  std::string am, ap, bm, bp; cut(a, am, ap); cut(b, bm, bp);
  auto ma = splitDots(am), mb = splitDots(bm);
  for (size_t i = 0; i < std::max(ma.size(), mb.size()); i++) {
    long x = i < ma.size() && isNum(ma[i]) ? std::stol(ma[i]) : 0, y = i < mb.size() && isNum(mb[i]) ? std::stol(mb[i]) : 0;
    if (x != y) return x < y ? -1 : 1;
  }
  if (ap.empty() != bp.empty()) return ap.empty() ? 1 : -1;   // a release beats its pre-releases
  auto pa = splitDots(ap), pb = splitDots(bp);
  for (size_t i = 0; i < std::max(pa.size(), pb.size()); i++) {
    if (i >= pa.size()) return -1;
    if (i >= pb.size()) return 1;
    bool na = isNum(pa[i]), nb = isNum(pb[i]);
    if (na && nb) { long x = std::stol(pa[i]), y = std::stol(pb[i]); if (x != y) return x < y ? -1 : 1; }
    else if (na != nb) return na ? -1 : 1;
    else if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
  }
  return 0;
}

std::string niceVersion(const std::string& v0) {
  std::string v = v0;
  if (!v.empty() && v[0] == 'v') v.erase(0, 1);
  size_t p = v.find("-alpha");
  if (p != std::string::npos) {
    std::string rest = v.substr(p + 6);
    if (rest.empty()) return v.substr(0, p);
    if (rest[0] == '.' && isNum(rest.substr(1))) return v.substr(0, p) + rest;
  }
  return v;
}

bool validRepoSource(const std::string& s) {
  size_t sl = s.find('/');
  if (sl == std::string::npos || sl == 0 || sl > 39 || s.find('/', sl + 1) != std::string::npos) return false;
  std::string o = s.substr(0, sl), n = s.substr(sl + 1);
  if (n.empty() || n.size() > 100 || !std::isalnum((unsigned char)o[0])) return false;
  for (char c : o) if (!std::isalnum((unsigned char)c) && c != '-') return false;
  for (char c : n) if (!std::isalnum((unsigned char)c) && c != '.' && c != '_' && c != '-') return false;
  return true;
}

std::string platformName() {
#if defined(_WIN32)
  return "windows";
#elif defined(__linux__)
  return "linux";
#else
  return "";
#endif
}

bool parseRelease(const json& j, ReleaseInfo& r, std::string& err) {
  if (!j.is_object()) { err = "GitHub sent something that is not a release."; return false; }
  if (j.contains("message") && !j.contains("tag_name")) { err = "GitHub: " + j.value("message", std::string()); return false; }
  try {
    r.tag = j.value("tag_name", "");
    r.name = j.contains("name") && j["name"].is_string() ? j["name"].get<std::string>() : r.tag;
    r.body = j.contains("body") && j["body"].is_string() ? j["body"].get<std::string>() : "";
    r.htmlUrl = j.value("html_url", "");
    r.prerelease = j.value("prerelease", false);
    r.version = r.tag;
    if (!r.version.empty() && (r.version[0] == 'v' || r.version[0] == 'V')) r.version.erase(0, 1);
    r.assets.clear();
    if (j.contains("assets") && j["assets"].is_array())
      for (auto& a : j["assets"]) {
        ReleaseAsset ra;
        ra.name = a.value("name", "");
        ra.url = a.value("browser_download_url", "");
        ra.size = a.value("size", (i64)0);
        if (a.contains("digest") && a["digest"].is_string()) ra.digest = a["digest"];
        if (!ra.name.empty() && !ra.url.empty()) r.assets.push_back(ra);
      }
  } catch (const std::exception& e) { err = std::string("Could not read the release: ") + e.what(); return false; }
  if (r.version.empty()) { err = "The newest release has no version tag."; return false; }
  return true;
}

const ReleaseAsset* pickAsset(const ReleaseInfo& r, const std::string& plat) {
  const char* suffix = plat == "windows" ? "-windows-x64.zip" : plat == "linux" ? "-linux-x64.tar.gz" : nullptr;
  if (!suffix) return nullptr;
  for (auto& a : r.assets) if (endsWith(toLower(a.name), suffix)) return &a;
  return nullptr;
}

// ---------------------------------------------------------------- SHA-256 (FIPS 180-4)
namespace {
struct Sha256 {
  u32 h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  u8 buf[64]; size_t n = 0; u64 total = 0;
  static u32 ror(u32 x, int r) { return (x >> r) | (x << (32 - r)); }
  void block(const u8* p) {
    static const u32 K[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    u32 w[64];
    for (int i = 0; i < 16; i++) w[i] = (u32)p[i * 4] << 24 | (u32)p[i * 4 + 1] << 16 | (u32)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
      u32 s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
      u32 t1 = hh + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
      u32 t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
  void add(const u8* p, size_t len) {
    total += len;
    while (len) {
      size_t k = std::min(len, 64 - n); std::memcpy(buf + n, p, k); n += k; p += k; len -= k;
      if (n == 64) { block(buf); n = 0; }
    }
  }
  std::string hex() {
    u64 bits = total * 8; u8 pad = 0x80; add(&pad, 1); u8 z = 0;
    while (n != 56) add(&z, 1);
    u8 l[8]; for (int i = 0; i < 8; i++) l[i] = (u8)(bits >> (56 - i * 8));
    add(l, 8);
    std::string s; char t[9];
    for (u32 v : h) { std::snprintf(t, sizeof t, "%08x", v); s += t; }
    return s;
  }
};
}

std::string sha256Hex(const void* data, size_t len) { Sha256 s; s.add((const u8*)data, len); return s.hex(); }
std::string sha256File(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return "";
  Sha256 s; std::vector<char> b(1 << 16);
  while (f) { f.read(b.data(), b.size()); if (f.gcount() > 0) s.add((const u8*)b.data(), (size_t)f.gcount()); }
  return s.hex();
}

// ---------------------------------------------------------------- installed versions
fs::path Updater::tmpDir() const { fs::path d = userSub("versions") / ".download"; std::error_code ec; fs::create_directories(d, ec); return d; }
json Updater::loadState() const {
  auto j = readJson(userSub("versions") / "versions.json");
  json s = j && j->is_object() ? *j : json::object();
  if (!s.contains("installed") || !s["installed"].is_object()) s["installed"] = json::object();
  if (!s.contains("bad") || !s["bad"].is_object()) s["bad"] = json::object();
  return s;
}
void Updater::saveState(const json& j) const { writeJson(userSub("versions") / "versions.json", j); }
std::string Updater::activeVersion() const { return loadState().value("active", ""); }

fs::path Updater::executable(const std::string& base, std::string* version) const {
  json s = loadState();
  for (const char* k : {"active", "previous"}) {
    std::string v = s.value(k, "");
    if (v.empty() || s["bad"].contains(v) || compareVersions(v, DQ_VERSION) <= 0 || !s["installed"].contains(v)) continue;
    fs::path dir = s["installed"][v].value("dir", "");
    fs::path exe = dir / exeName(base);
    if (!dir.empty() && fs::exists(exe)) { if (version) *version = v; return exe; }
  }
  if (version) *version = DQ_VERSION;
  return installDir() / exeName(base);
}
std::string Updater::currentVersion() const {
  std::string v; executable("dokuquest", &v);
  return v;
}
void Updater::markBad(const std::string& v, const std::string& why) {
  if (v == DQ_VERSION) return;
  json s = loadState(); s["bad"][v] = why; saveState(s);
  DQ_WARN("updater: version %s marked bad (%s); falling back", v.c_str(), why.c_str());
}

// ---------------------------------------------------------------- the check / install state machine
void Updater::fail(const std::string& msg) { state = State::Failed; message = msg; DQ_WARN("updater: %s", msg.c_str()); }

bool Updater::spawnCurl(const std::string& url, const fs::path& out, bool api) {
  std::vector<std::string> a = {"-sS", "-L", "--connect-timeout", "10", "-A", "DokuQuest-launcher"};
  if (api) a.insert(a.end(), {"--max-time", "30", "-H", "Accept: application/vnd.github+json", "-H", "X-GitHub-Api-Version: 2022-11-28", "-D", (tmpDir() / "release.hdr").string()});
  else a.insert(a.end(), {"--fail", "--retry", "3", "-H", "Accept: application/octet-stream"});
  a.insert(a.end(), {"-o", out.string(), url});
  ProcOptions o; o.hidden = true; o.searchPath = true; o.stderrFile = tmpDir() / "curl.err";
  std::error_code ec; fs::remove(out, ec);
  if (api) fs::remove(tmpDir() / "release.hdr", ec);
  if (!proc.start(exeName("curl"), a, o)) { fail("Updates need curl, and it was not found on this computer."); return false; }
  return true;
}

void Updater::check(bool q) {
  if (busy()) return;
  quiet = q; installed.clear(); progress = 0;
  if (!validRepoSource(source)) { fail("Write the repository as owner/name, like FartGarfunkel/DokuQuest-Rewrite."); return; }
  std::string url = apiOverride.empty() ? "https://api.github.com/repos/" + source + "/releases/latest" : apiOverride;
  DQ_LOG("updater: checking %s", url.c_str());
  if (spawnCurl(url, tmpDir() / "release.json", true)) { state = State::Checking; message = "Checking…"; }
}

static int httpStatus(const fs::path& hdr) {
  auto h = readFile(hdr);
  if (!h || h->empty()) return 200;   // file:// (tests) has no headers
  int st = 0;
  for (auto& line : splitStr(*h, '\n'))
    if (startsWith(line, "HTTP/")) { auto sp = line.find(' '); if (sp != std::string::npos) st = std::atoi(line.c_str() + sp + 1); }
  return st ? st : 200;
}
static std::string curlProblem(int code) {
  switch (code) {
    case 5: case 6: case 7: case 28: case 35: case 52: case 55: case 56: case 60: return "Could not reach GitHub. Are you online?";
    case 127: return "Updates need curl, and it was not found on this computer.";
    default: return strFormat("Could not reach GitHub (curl error %d).", code);
  }
}

void Updater::poll() {
  if (!busy()) return;
  if (state == State::Downloading && asset.size > 0) {
    std::error_code ec; auto sz = fs::file_size(tmpDir() / (asset.name + ".part"), ec);
    if (!ec) progress = clampv((float)((double)sz / asset.size), 0.f, 1.f);
  }
  if (proc.running()) return;
  int code = proc.exitCode();
  if (state == State::Checking) {
    if (code != 0) { fail(curlProblem(code)); return; }
    int st = httpStatus(tmpDir() / "release.hdr");
    if (st == 404) { fail("No releases on " + source + " yet."); return; }
    if (st == 401) { fail("GitHub did not accept the request (401)."); return; }
    if (st == 403 || st == 429) { fail("GitHub asks us to wait a while (too many checks). Try again later."); return; }
    if (st < 200 || st >= 300) { fail(strFormat("GitHub answered %d.", st)); return; }
    auto j = readJson(tmpDir() / "release.json");
    std::string err;
    if (!j) { fail("GitHub's answer could not be read."); return; }
    if (!parseRelease(*j, rel, err)) { fail(err); return; }
    latest = rel.version;
    std::string cur = currentVersion();
    DQ_LOG("updater: newest release %s (%zu assets), running %s", rel.version.c_str(), rel.assets.size(), cur.c_str());
    if (compareVersions(rel.version, cur) <= 0) { state = State::Done; message = "You have the newest game (" + niceVersion(cur) + ")."; return; }
    json s = loadState();
    if (s["bad"].contains(rel.version)) { state = State::Done; message = "Version " + niceVersion(rel.version) + " did not start on this computer before, so it is skipped."; return; }
    const ReleaseAsset* a = pickAsset(rel, platformName());
    if (!a) { fail("Release " + rel.tag + " has no download for this computer."); return; }
    if (a->size > APP_MAX) { fail(a->name + " is unexpectedly large."); return; }
    asset = *a;
    wantSha = startsWith(asset.digest, "sha256:") ? toLower(asset.digest.substr(7)) : "";
    if (wantSha.empty())
      for (auto& x : rel.assets) if (toLower(x.name) == "sha256sums.txt" || toLower(x.name) == toLower(asset.name) + ".sha256") {
        if (spawnCurl(x.url, tmpDir() / "sums.txt", false)) { state = State::DownloadingSums; message = "Downloading " + niceVersion(rel.version) + "…"; }
        return;
      }
    startDownload();
    return;
  }
  if (state == State::DownloadingSums) {
    if (code == 0)
      if (auto t = readFile(tmpDir() / "sums.txt"))
        for (auto& line : splitStr(*t, '\n')) {
          auto parts = splitStr(line, ' ');
          parts.erase(std::remove(parts.begin(), parts.end(), std::string()), parts.end());
          if (parts.size() == 1 && parts[0].size() == 64) wantSha = toLower(parts[0]);   // <asset>.sha256 with just the hash
          if (parts.size() >= 2) { std::string n = parts.back(); if (!n.empty() && n[0] == '*') n.erase(0, 1); if (n == asset.name) wantSha = toLower(parts[0]); }
        }
    startDownload();
    return;
  }
  if (state == State::Downloading) { finishDownload(); return; }
  if (state == State::Extracting) { finishExtract(); return; }
}

void Updater::startDownload() {
  DQ_LOG("updater: downloading %s (%lld bytes, sha256 %s)", asset.name.c_str(), (long long)asset.size, wantSha.empty() ? "unknown" : wantSha.c_str());
  if (spawnCurl(asset.url, tmpDir() / (asset.name + ".part"), false)) { state = State::Downloading; progress = 0; message = "Downloading " + niceVersion(rel.version) + "…"; }
}

void Updater::finishDownload() {
  int code = proc.exitCode();
  fs::path part = tmpDir() / (asset.name + ".part");
  std::error_code ec;
  if (code == 22) { fail("Could not download " + asset.name + "."); return; }
  if (code != 0) { fail("The download was cut off. Try again."); return; }
  i64 got = (i64)fs::file_size(part, ec);
  if (ec || (asset.size && got != asset.size)) { fs::remove(part, ec); fail("The download of " + asset.name + " was incomplete. Try again."); return; }
  if (!wantSha.empty() && sha256File(part) != wantSha) { fs::remove(part, ec); fail(asset.name + " did not match its checksum, so it was not used."); return; }
  fs::path dest = userSub("versions") / (rel.version + ".part");
  fs::remove_all(dest, ec); fs::create_directories(dest, ec);
  std::vector<std::string> a = platformName() == "windows" ? std::vector<std::string>{"-xf", part.string(), "-C", dest.string()}
                                                           : std::vector<std::string>{"-xzf", part.string(), "-C", dest.string()};
  ProcOptions o; o.hidden = true; o.searchPath = true; o.stderrFile = tmpDir() / "tar.err";
  if (!proc.start(exeName("tar"), a, o)) { fail("Could not unpack the update (tar was not found)."); return; }
  state = State::Extracting; progress = 1; message = "Unpacking " + niceVersion(rel.version) + "…";
}

void Updater::finishExtract() {
  std::error_code ec;
  fs::path vroot = userSub("versions"), part = vroot / (rel.version + ".part"), dest = vroot / rel.version;
  if (proc.exitCode() != 0) { fs::remove_all(part, ec); fail("Could not unpack " + asset.name + "."); return; }
  // the archive holds one folder (DokuQuest/) with the programs, data/ and assets/
  fs::path found;
  auto hasGame = [](const fs::path& d) { return fs::exists(d / exeName("dokuquest")) && fs::exists(d / "data"); };
  if (hasGame(part)) found = part;
  else for (auto& e : fs::directory_iterator(part, ec)) if (e.is_directory() && hasGame(e.path())) { found = e.path(); break; }
  if (found.empty()) { fs::remove_all(part, ec); fail("The download is not a DokuQuest build."); return; }
#ifndef _WIN32
  for (const char* b : {"dokuquest", "dokuquest-launcher", "dokuquest-server"}) {
    fs::path p = found / b;
    if (fs::exists(p)) fs::permissions(p, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
  }
#endif
  fs::path rel2 = fs::relative(found, part, ec);
  fs::remove_all(dest, ec);
  fs::rename(part, dest, ec);
  if (ec) { fail("Could not put the new version in place: " + ec.message()); return; }
  fs::remove(tmpDir() / (asset.name + ".part"), ec);
  json s = loadState();
  std::string old = s.value("active", "");
  if (!old.empty() && old != rel.version) s["previous"] = old;
  s["active"] = rel.version;
  s["installed"][rel.version] = {{"dir", (dest / rel2).lexically_normal().string()}, {"asset", asset.name}, {"sha256", wantSha}, {"at", unixMillis()}};
  // keep the active and the previous version, remove older ones
  std::string prev = s.value("previous", "");
  for (auto it = s["installed"].begin(); it != s["installed"].end();) {
    if (it.key() != rel.version && it.key() != prev) { fs::remove_all(vroot / it.key(), ec); it = s["installed"].erase(it); }
    else ++it;
  }
  saveState(s);
  installed = rel.version;
  state = State::Done;
  message = "Installed " + niceVersion(rel.version) + ". Press Play.";
  DQ_LOG("updater: installed %s into %s", rel.version.c_str(), dest.string().c_str());
}

} // namespace dq
