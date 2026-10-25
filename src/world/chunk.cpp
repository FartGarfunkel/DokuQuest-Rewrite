#include "world/chunk.h"
#include <cstring>

namespace dq {

Section* Chunk::ensure(int sy) {
  if (!secs[sy]) {
    secs[sy] = std::make_unique<Section>();
    std::memset(secs[sy]->light, airLight[sy], sizeof(secs[sy]->light));
  }
  return secs[sy].get();
}

void Chunk::set(int x, int y, int z, BlockId id, u8 meta) {
  if ((unsigned)y >= DIM_H) return;
  Section* s = secs[y >> 4].get();
  if (!s) { if (id == AIR && meta == 0) return; s = ensure(y >> 4); }
  int i = secIdx(x, y & 15, z);
  BlockId old = s->blocks[i];
  if (old == AIR && id != AIR) s->solidCount++;
  else if (old != AIR && id == AIR) s->solidCount--;
  s->blocks[i] = id; s->meta[i] = meta;
  int hi = z * 16 + x;
  if (id != AIR) { if (y + 1 > maxY) maxY = y + 1; if (y > heightMap[hi]) heightMap[hi] = (u8)y; }
  else if (y == heightMap[hi]) { int yy = y; while (yy > 0 && get(x, yy, z) == AIR) yy--; heightMap[hi] = (u8)yy; }
}

void Chunk::recomputeMaxY() {
  maxY = 0;
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    int y = DIM_H - 1;
    while (y > 0 && get(x, y, z) == AIR) y--;
    heightMap[z * 16 + x] = (u8)y;
    if (get(x, y, z) != AIR && y + 1 > maxY) maxY = y + 1;
  }
}
void Chunk::compact() {
  for (int sy = 0; sy < SECS; sy++) if (secs[sy]) {
    u16 n = 0; for (int i = 0; i < 4096; i++) if (secs[sy]->blocks[i]) n++;
    secs[sy]->solidCount = n;
    if (!n) {
      // an all-air section may only go if its light is uniform too (shade under an overhang, a lit cave)
      const u8* L = secs[sy]->light; bool uniform = true;
      for (int i = 1; i < 4096 && uniform; i++) uniform = L[i] == L[0];
      if (uniform) { airLight[sy] = L[0]; secs[sy].reset(); }
    }
  }
}

// ---- codec ----
static void putVar(std::vector<u8>& o, u32 n) { while (n >= 128) { o.push_back((u8)((n & 127) | 128)); n >>= 7; } o.push_back((u8)n); }
static bool getVar(const u8*& p, const u8* e, u32& n) { n = 0; int sh = 0; while (p < e) { u8 b = *p++; n |= (u32)(b & 127) << sh; sh += 7; if (!(b & 128)) return true; } return false; }

std::vector<u8> encodeChunk(const Chunk& c) {
  // header: "DQC1" | palette (json) | rle(blocks) | rle(meta) | block entities json | extra json
  std::vector<std::string> palette; std::unordered_map<BlockId, u16> map;
  std::vector<u16> pal(DIM_H * 256);
  int k = 0;
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) for (int y = 0; y < DIM_H; y++) {
    BlockId id = c.get(x, y, z);
    auto it = map.find(id);
    u16 p;
    if (it == map.end()) { p = (u16)palette.size(); map[id] = p; palette.push_back(reg().block(id).key); } else p = it->second;
    pal[k++] = p;
  }
  std::vector<u8> raw;
  json head; head["palette"] = palette; head["dim"] = c.dim; head["cx"] = c.cx; head["cz"] = c.cz; head["t"] = c.savedAt;
  json be = json::object();
  for (auto& [i, e] : c.blockEntities) { int x = i & 15, z = (i >> 4) & 15, y = i >> 8; be[std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z)] = e; }
  head["be"] = be;
  if (!c.extra.is_null()) head["x"] = c.extra;
  std::string hs = head.dump();
  putVar(raw, (u32)hs.size()); raw.insert(raw.end(), hs.begin(), hs.end());
  bool wide = palette.size() > 255;
  auto rle = [&](auto getv, bool w) {
    int prev = -1; u32 run = 0;
    auto flush = [&] { if (run) { putVar(raw, run); raw.push_back((u8)(prev & 255)); if (w) raw.push_back((u8)(prev >> 8)); } };
    for (int i = 0; i < DIM_H * 256; i++) { int v = getv(i); if (v == prev) run++; else { flush(); prev = v; run = 1; } }
    flush();
  };
  raw.push_back(wide ? 1 : 0);
  rle([&](int i) { return (int)pal[i]; }, wide);
  rle([&](int i) { int z = i / (16 * DIM_H), x = (i / DIM_H) & 15, y = i % DIM_H; return (int)c.getMeta(x, y, z); }, false);
  auto z = zCompress(raw.data(), raw.size(), 6);
  std::vector<u8> out = {'D', 'Q', 'C', '1'};
  u32 rs = (u32)raw.size();
  for (int b = 0; b < 4; b++) out.push_back((u8)(rs >> (b * 8)));
  out.insert(out.end(), z.begin(), z.end());
  return out;
}

bool decodeChunk(Chunk& c, const u8* data, size_t n) {
  if (n < 8 || std::memcmp(data, "DQC1", 4) != 0) return false;
  u32 rs = data[4] | data[5] << 8 | data[6] << 16 | (u32)data[7] << 24;
  std::vector<u8> raw;
  if (!zDecompress(data + 8, n - 8, raw, rs)) return false;
  const u8* p = raw.data(); const u8* e = p + raw.size();
  u32 hl; if (!getVar(p, e, hl) || p + hl > e) return false;
  json head;
  try { head = json::parse(std::string((const char*)p, hl)); } catch (...) { return false; }
  p += hl;
  std::vector<BlockId> ids;
  for (auto& k : head["palette"]) ids.push_back(reg().blockId(k.get<std::string>()));
  if (p >= e) return false;
  bool wide = *p++ != 0;
  std::vector<u16> pal(DIM_H * 256);
  std::vector<u8> meta(DIM_H * 256);
  auto unrle = [&](auto setv, bool w) {
    int k = 0;
    while (p < e && k < DIM_H * 256) {
      u32 run; if (!getVar(p, e, run)) return false;
      if (p + (w ? 2 : 1) > e) return false;
      int v = *p++; if (w) v |= *p++ << 8;
      for (u32 i = 0; i < run && k < DIM_H * 256; i++) setv(k++, v);
    }
    return k == DIM_H * 256;
  };
  if (!unrle([&](int k, int v) { pal[k] = (u16)v; }, wide)) return false;
  unrle([&](int k, int v) { meta[k] = (u8)v; }, false);
  int k = 0;
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) for (int y = 0; y < DIM_H; y++, k++) {
    BlockId id = pal[k] < ids.size() ? ids[pal[k]] : AIR;
    if (id || meta[k]) c.set(x, y, z, id, meta[k]);
  }
  c.blockEntities.clear();
  if (head.contains("be")) for (auto& [pos, ent] : head["be"].items()) {
    auto parts = splitStr(pos, ',');
    if (parts.size() == 3) c.blockEntities[colIdx(std::stoi(parts[0]), std::stoi(parts[1]), std::stoi(parts[2]))] = ent;
  }
  if (head.contains("x")) c.extra = head["x"];
  c.savedAt = head.value("t", (i64)0);
  c.modified = true;
  c.recomputeMaxY();
  return true;
}

} // namespace dq
