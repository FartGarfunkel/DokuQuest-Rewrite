// assets/pack_fs.cpp — resource pack file access (folders and zips via miniz).
#include "assets/pack_fs.h"
#include <miniz.h>
#include <algorithm>
#include <cstring>

namespace dq {

static std::unique_ptr<PackFS>& activeSlot() { static std::unique_ptr<PackFS> p; return p; }
PackFS* activePack() { return activeSlot().get(); }
void setActivePack(std::unique_ptr<PackFS> p) { activeSlot() = std::move(p); }

PackFS::~PackFS() {
  if (za) { mz_zip_reader_end((mz_zip_archive*)za); delete (mz_zip_archive*)za; }
}

std::unique_ptr<PackFS> PackFS::open(const fs::path& path) {
  std::unique_ptr<PackFS> P(new PackFS());
  P->path_ = path;
  std::string fn = path.filename().string();
  std::error_code ec;
  std::vector<std::string> raw;
  if (fs::is_directory(path, ec)) {
    P->name_ = fn;
    for (auto it = fs::recursive_directory_iterator(path, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (!it->is_regular_file(ec)) continue;
      std::string r = fs::relative(it->path(), path, ec).generic_string();
      if (!ec) raw.push_back(r);
    }
  } else {
    if (!endsWith(toLower(fn), ".zip")) return nullptr;
    P->name_ = fn.substr(0, fn.size() - 4);
    P->zip = true;
    auto* z = new mz_zip_archive(); memset(z, 0, sizeof(*z));
    if (!mz_zip_reader_init_file(z, path.string().c_str(), 0)) { delete z; DQ_WARN("resource pack %s: not a readable zip", fn.c_str()); return nullptr; }
    P->za = z;
    mz_uint n = mz_zip_reader_get_num_files(z);
    char buf[1024];
    for (mz_uint i = 0; i < n; i++) {
      if (mz_zip_reader_is_file_a_directory(z, i)) continue;
      mz_zip_reader_get_filename(z, i, buf, sizeof buf);
      raw.push_back(buf);
      P->zidx[buf] = i;
    }
  }
  // the pack root: the folder that holds assets/ (or pack.mcmeta) — a zip may wrap it in a folder
  for (auto& r : raw) {
    size_t i = r.find("assets/minecraft/");
    if (i != std::string::npos) { P->base = r.substr(0, i); break; }
  }
  if (P->base.empty()) for (auto& r : raw) {
    size_t i = r.find("textures/blocks/");
    if (i != std::string::npos) { P->base = r.substr(0, i); break; }
  }
  if (P->base.empty()) for (auto& r : raw) if (endsWith(r, "pack.mcmeta")) { P->base = r.substr(0, r.size() - 11); break; }
  for (auto& r : raw) if (startsWith(r, P->base)) P->files.push_back(r.substr(P->base.size()));
  std::sort(P->files.begin(), P->files.end());
  // where the textures live (render/packs.js: assets/minecraft/textures/, else the old textures/blocks)
  if (!P->list("assets/minecraft/textures/").empty()) P->texRoot = "assets/minecraft/textures/";
  else if (!P->list("textures/blocks/").empty()) { P->texRoot = "textures/"; P->legacy = true; }
  if (auto mc = P->readJson("pack.mcmeta")) {
    const json& pk = (*mc)["pack"];
    if (pk.is_object()) {
      P->packFormat = pk.value("pack_format", 0);
      const json& d = pk["description"];
      std::string s = d.is_string() ? d.get<std::string>() : d.is_object() && d.contains("text") && d["text"].is_string() ? d["text"].get<std::string>() : "";
      std::string o; for (size_t i = 0; i < s.size(); i++) { if ((u8)s[i] == 0xC2 && i + 1 < s.size() && (u8)s[i + 1] == 0xA7) { i += 2; continue; } o += s[i]; }
      P->description = o;
    }
  }
  return P;
}

bool PackFS::exists(const std::string& rel) const { return std::binary_search(files.begin(), files.end(), rel); }

std::optional<std::vector<u8>> PackFS::read(const std::string& rel) const {
  if (!exists(rel)) return std::nullopt;
  if (!zip) return readBinary(path_ / rel);
  std::lock_guard<std::mutex> lk(mx);
  auto it = zidx.find(base + rel);
  if (it == zidx.end()) return std::nullopt;
  size_t n = 0;
  void* p = mz_zip_reader_extract_to_heap((mz_zip_archive*)za, it->second, &n, 0);
  if (!p) return std::nullopt;
  std::vector<u8> out((u8*)p, (u8*)p + n);
  mz_free(p);
  return out;
}

std::optional<json> PackFS::readJson(const std::string& rel) const {
  auto b = read(rel);
  if (!b) return std::nullopt;
  json j = json::parse(b->begin(), b->end(), nullptr, false);
  if (j.is_discarded()) return std::nullopt;
  return j;
}

std::vector<std::string> PackFS::list(const std::string& prefix) const {
  std::vector<std::string> out;
  for (auto it = std::lower_bound(files.begin(), files.end(), prefix); it != files.end() && startsWith(*it, prefix); ++it) out.push_back(*it);
  return out;
}

} // namespace dq
