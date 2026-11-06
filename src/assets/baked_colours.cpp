// assets/baked_colours.cpp — read and write the colour bake. The bytes are linked into dokuquest;
// a build without that object keeps the weak symbols empty and falls back to the PNG files.
#include "assets/baked_colours.h"
#include "common/common.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace dq {

namespace {

extern "C" {
extern const unsigned char _binary_baked_colours_bin_start[] __attribute__((weak));
extern const unsigned char _binary_baked_colours_bin_end[] __attribute__((weak));
}

struct StoredImg { int w = 0, h = 0; const u8* px = nullptr; };
std::unordered_map<std::string, StoredImg> g_images;
bool g_on = false;

struct Reader {
  const u8* p = nullptr;
  const u8* end = nullptr;
  bool ok = true;
  u32 u32le() {
    if (p + 4 > end) { ok = false; return 0; }
    u32 v = (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
    p += 4;
    return v;
  }
  u16 u16le() {
    if (p + 2 > end) { ok = false; return 0; }
    u16 v = (u16)p[0] | ((u16)p[1] << 8);
    p += 2;
    return v;
  }
  std::string str() {
    u16 n = u16le();
    if (!ok || p + n > end) { ok = false; return {}; }
    std::string s(reinterpret_cast<const char*>(p), n);
    p += n;
    return s;
  }
  bool take(void* dst, size_t n) {
    if (!ok || p + n > end) { ok = false; return false; }
    if (n) std::memcpy(dst, p, n);
    p += n;
    return true;
  }
  // Leaves the bytes in the linked bake. They stay for the life of the program.
  bool skip(size_t n) {
    if (!ok || p + n > end) { ok = false; return false; }
    p += n;
    return true;
  }
};

bool parseBaked(const u8* data, size_t n, TileSet& ts) {
  Reader r{data, data + n, true};
  char magic[8];
  if (!r.take(magic, 8) || std::memcmp(magic, "DQVOXEL1", 8) != 0) return false;
  if (r.u32le() != 1) return false;
  const u32 tileSize = r.u32le(), tileCount = r.u32le(), animCount = r.u32le(), imageCount = r.u32le();
  const u32 grassW = r.u32le(), grassH = r.u32le(), foliageW = r.u32le(), foliageH = r.u32le();
  if (!r.ok || tileSize < 8 || tileSize > 64 || tileCount == 0 || tileCount > 20000) return false;
  const size_t pix = (size_t)tileSize * tileSize;
  ts = TileSet();
  ts.size = (int)tileSize;
  ts.count = (int)tileCount;
  ts.rgba.resize((size_t)tileCount * pix * 4);
  ts.res.assign(tileCount, (u8)tileSize);
  if (!r.take(ts.rgba.data(), ts.rgba.size())) return false;
  if (!r.take(ts.res.data(), tileCount)) return false;
  const u32 nameCount = r.u32le();
  if (!r.ok || nameCount > 100000u) return false;
  for (u32 i = 0; i < nameCount; i++) {
    std::string name = r.str();
    u32 layer = r.u32le();
    if (!r.ok || layer >= tileCount || name.empty()) return false;
    ts.index.emplace(std::move(name), (int)layer);
  }
  ts.anims.clear();
  for (u32 i = 0; i < animCount; i++) {
    TileAnim a;
    a.layer = (int)r.u32le();
    a.frameTime = (int)r.u32le();
    u32 nf = r.u32le();
    if (!r.ok || nf > 512 || a.layer < 0 || a.layer >= ts.count) return false;
    a.frames.resize(nf);
    for (u32 f = 0; f < nf; f++) {
      a.frames[f] = (int)r.u32le();
      if (!r.ok || a.frames[f] < 0 || a.frames[f] >= ts.count) return false;
    }
    if (a.frameTime < 1) a.frameTime = 50;
    ts.anims.push_back(std::move(a));
  }
  g_images.clear();
  for (u32 i = 0; i < imageCount; i++) {
    BakedImage im;
    im.key = r.str();
    im.w = (int)r.u32le();
    im.h = (int)r.u32le();
    if (!r.ok || im.key.empty() || im.w <= 0 || im.h <= 0 || im.w > 2048 || im.h > 8192) return false;
    const u8* px = r.p;
    if (!r.skip((size_t)im.w * (size_t)im.h * 4)) return false;
    g_images.emplace(std::move(im.key), StoredImg{im.w, im.h, px});
  }
  std::vector<u8> grass, foliage;
  if (grassW && grassH) {
    if (grassW > 2048 || grassH > 2048) return false;
    grass.resize((size_t)grassW * grassH * 4);
    if (!r.take(grass.data(), grass.size())) return false;
  }
  if (foliageW && foliageH) {
    if (foliageW > 2048 || foliageH > 2048) return false;
    foliage.resize((size_t)foliageW * foliageH * 4);
    if (!r.take(foliage.data(), foliage.size())) return false;
  }
  if (!r.ok) return false;
  setPackColormaps((int)grassW, (int)grassH, grass.empty() ? nullptr : grass.data(),
                   (int)foliageW, (int)foliageH, foliage.empty() ? nullptr : foliage.data());
  ts.packName = "DokuQuest Base";
  deriveMicroVoxelHeights(ts);
  return true;
}

void putU32(FILE* f, u32 v) {
  u8 b[4] = {(u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24)};
  std::fwrite(b, 1, 4, f);
}
void putU16(FILE* f, u16 v) {
  u8 b[2] = {(u8)v, (u8)(v >> 8)};
  std::fwrite(b, 1, 2, f);
}
void putStr(FILE* f, const std::string& s) {
  putU16(f, (u16)std::min(s.size(), (size_t)65535));
  if (!s.empty()) std::fwrite(s.data(), 1, std::min(s.size(), (size_t)65535), f);
}

} // namespace

bool loadBakedColours(TileSet& ts) {
  g_on = false;
  g_images.clear();
  if (!_binary_baked_colours_bin_start || !_binary_baked_colours_bin_end) return false;
  const size_t n = (size_t)(_binary_baked_colours_bin_end - _binary_baked_colours_bin_start);
  if (n < 32) return false;
  if (!parseBaked(_binary_baked_colours_bin_start, n, ts)) {
    g_images.clear();
    g_on = false;
    DQ_WARN("baked colours: the linked table could not be read");
    return false;
  }
  g_on = true;
  DQ_LOG("baked colours: %d tiles, %d creature pictures, carried in the game", ts.count, (int)g_images.size());
  return true;
}

bool bakedColoursOn() { return g_on; }
void dropBakedPictures() { g_images.clear(); g_on = false; }

bool bakedImage(const std::string& key, int& w, int& h, std::vector<u8>& rgba) {
  auto it = g_images.find(key);
  if (it == g_images.end() || !it->second.px) return false;
  w = it->second.w;
  h = it->second.h;
  rgba.assign(it->second.px, it->second.px + (size_t)w * (size_t)h * 4);
  return true;
}

bool writeBakedColours(const fs::path& path, const TileSet& ts, const std::vector<BakedImage>& images,
                       int grassW, int grassH, const u8* grass, int foliageW, int foliageH, const u8* foliage) {
  FILE* f = std::fopen(path.string().c_str(), "wb");
  if (!f) return false;
  const size_t pix = (size_t)ts.size * ts.size;
  if (ts.count <= 0 || ts.size <= 0 || ts.rgba.size() < (size_t)ts.count * pix * 4) { std::fclose(f); return false; }
  std::fwrite("DQVOXEL1", 1, 8, f);
  putU32(f, 1);
  putU32(f, (u32)ts.size);
  putU32(f, (u32)ts.count);
  putU32(f, (u32)ts.anims.size());
  putU32(f, (u32)images.size());
  putU32(f, grass && grassW > 0 && grassH > 0 ? (u32)grassW : 0);
  putU32(f, grass && grassW > 0 && grassH > 0 ? (u32)grassH : 0);
  putU32(f, foliage && foliageW > 0 && foliageH > 0 ? (u32)foliageW : 0);
  putU32(f, foliage && foliageW > 0 && foliageH > 0 ? (u32)foliageH : 0);
  std::fwrite(ts.rgba.data(), 1, (size_t)ts.count * pix * 4, f);
  std::vector<u8> res = ts.res;
  res.resize((size_t)ts.count, (u8)ts.size);
  std::fwrite(res.data(), 1, (size_t)ts.count, f);
  u32 named = 0;
  for (auto& kv : ts.index) if (!kv.first.empty() && kv.second >= 0 && kv.second < ts.count) named++;
  putU32(f, named);
  for (auto& kv : ts.index) {
    if (kv.first.empty() || kv.second < 0 || kv.second >= ts.count) continue;
    putStr(f, kv.first);
    putU32(f, (u32)kv.second);
  }
  for (const TileAnim& a : ts.anims) {
    putU32(f, (u32)a.layer);
    putU32(f, (u32)a.frameTime);
    putU32(f, (u32)a.frames.size());
    for (int fr : a.frames) putU32(f, (u32)fr);
  }
  for (const BakedImage& im : images) {
    putStr(f, im.key);
    putU32(f, (u32)im.w);
    putU32(f, (u32)im.h);
    std::fwrite(im.rgba.data(), 1, im.rgba.size(), f);
  }
  if (grass && grassW > 0 && grassH > 0) std::fwrite(grass, 1, (size_t)grassW * grassH * 4, f);
  if (foliage && foliageW > 0 && foliageH > 0) std::fwrite(foliage, 1, (size_t)foliageW * foliageH * 4, f);
  bool bad = std::ferror(f) != 0;
  std::fclose(f);
  return !bad;
}

} // namespace dq
