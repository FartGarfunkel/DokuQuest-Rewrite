// world/chunk.h — one 16 x 256 x 16 column of one dimension band, stored as 16 sections of 16³.
// Mirrors the original world/chunk.js Chunk (ids, meta, light = sky<<4 | block, block entities),
// with the height doubled to 256 and all-air sections left unallocated.
#pragma once
#include "world/dims.h"
#include "core/registry.h"
#include <array>
#include <atomic>
#include <unordered_map>

namespace dq {

inline int secIdx(int x, int y, int z) { return (y << 8) | (z << 4) | x; }      // within a section (y 0..15)
inline int colIdx(int x, int y, int z) { return (y << 8) | (z << 4) | x; }      // within a chunk band (y 0..255), same layout as the JS cidx

// Cubyz light: sun and block are each an RGB byte. A step subtracts 8 (about 31 blocks from full to dark).
// `light[]` stays the gameplay nibble (max channel >> 4), so mobs, spawn and shader-pack eye brightness still read 0..15.
struct LightRgb {
  u8 s[3]{};   // sun
  u8 b[3]{};   // block
  bool operator==(const LightRgb& o) const {
    return s[0] == o.s[0] && s[1] == o.s[1] && s[2] == o.s[2] && b[0] == o.b[0] && b[1] == o.b[1] && b[2] == o.b[2];
  }
};
inline u8 max3u(u8 a, u8 b, u8 c) { return a > b ? (a > c ? a : c) : (b > c ? b : c); }
inline u8 lightNibble(const LightRgb& L) {
  return (u8)((max3u(L.s[0], L.s[1], L.s[2]) >> 4) << 4 | (max3u(L.b[0], L.b[1], L.b[2]) >> 4));
}
// 5 bits per channel, the packing Cubyz's chunk shader unpacks: sun then block.
inline u32 packCubyzLight(const LightRgb& L) {
  return (u32)(L.s[0] >> 3) << 25 | (u32)(L.s[1] >> 3) << 20 | (u32)(L.s[2] >> 3) << 15
       | (u32)(L.b[0] >> 3) << 10 | (u32)(L.b[1] >> 3) << 5 | (u32)(L.b[2] >> 3);
}
// Bit 31 marks a vertex light word as real RGB (0 is also a valid dark cell).
inline u32 markCubyz(u32 pack) { return pack | 0x80000000u; }
inline LightRgb fullSunRgb() { return LightRgb{{255, 255, 255}, {0, 0, 0}}; }

struct Section {
  BlockId blocks[4096]{};
  u8 meta[4096]{};
  u8 carve[4096]{};   // chisel octant mask, kept OUT of meta so legacy metas (facing/stage/snow) can never masquerade as carved
  u8 coat[4096]{};    // snow on a carved top: low 4 bits = columns (hx<<1)|hz, bit 4 = grown to 4px
  u8 light[4096]{};     // (sky << 4) | block, derived from rgb
  LightRgb rgb[4096]{}; // sun RGB + block RGB, the light the meshes shade with
  u16 solidCount = 0;   // non-air blocks (0 -> may be freed)
};

struct ChunkKey {
  int dim, cx, cz;
  bool operator==(const ChunkKey& o) const { return dim == o.dim && cx == o.cx && cz == o.cz; }
};
struct ChunkKeyHash { size_t operator()(const ChunkKey& k) const { return ((size_t)(u32)k.cx * 73856093u) ^ ((size_t)(u32)k.cz * 19349663u) ^ ((size_t)k.dim * 83492791u); } };

class Chunk {
public:
  Chunk(int dim, int cx, int cz) : dim(dim), cx(cx), cz(cz) { airLight.fill(0xF0); airRgb.fill(fullSunRgb()); }
  const int dim, cx, cz;
  ChunkKey key() const { return {dim, cx, cz}; }
  int baseY() const { return dimBase(dim); }
  int worldX() const { return cx * CS; }
  int worldZ() const { return cz * CS; }

  // band-local accessors (x,z 0..15, y 0..255)
  BlockId get(int x, int y, int z) const { if ((unsigned)y >= DIM_H) return AIR; const Section* s = secs[y >> 4].get(); return s ? s->blocks[secIdx(x, y & 15, z)] : AIR; }
  u8 getMeta(int x, int y, int z) const { if ((unsigned)y >= DIM_H) return 0; const Section* s = secs[y >> 4].get(); return s ? s->meta[secIdx(x, y & 15, z)] : 0; }
  u8 getCarve(int x, int y, int z) const { if ((unsigned)y >= DIM_H) return 0; const Section* s = secs[y >> 4].get(); return s ? s->carve[secIdx(x, y & 15, z)] : 0; }
  u8 getCoat(int x, int y, int z) const { if ((unsigned)y >= DIM_H) return 0; const Section* s = secs[y >> 4].get(); return s ? s->coat[secIdx(x, y & 15, z)] : 0; }
  u8 getLight(int x, int y, int z) const { if ((unsigned)y >= DIM_H) return 0xF0; const Section* s = secs[y >> 4].get(); return s ? s->light[secIdx(x, y & 15, z)] : airLight[y >> 4]; }
  LightRgb getLightRgb(int x, int y, int z) const { if ((unsigned)y >= DIM_H) return fullSunRgb(); const Section* s = secs[y >> 4].get(); return s ? s->rgb[secIdx(x, y & 15, z)] : airRgb[y >> 4]; }
  void set(int x, int y, int z, BlockId id, u8 meta = 0);
  void setMeta(int x, int y, int z, u8 meta) { if (Section* s = secs[y >> 4].get()) s->meta[secIdx(x, y & 15, z)] = meta; }
  void setCarve(int x, int y, int z, u8 carve) { Section* s = ensure(y >> 4); s->carve[secIdx(x, y & 15, z)] = carve; }
  void setCoat(int x, int y, int z, u8 coat) { Section* s = ensure(y >> 4); s->coat[secIdx(x, y & 15, z)] = coat; }
  void setLight(int x, int y, int z, u8 l) { Section* s = ensure(y >> 4); s->light[secIdx(x, y & 15, z)] = l; }
  void setLightRgb(int x, int y, int z, const LightRgb& L) { Section* s = ensure(y >> 4); int i = secIdx(x, y & 15, z); s->rgb[i] = L; s->light[i] = lightNibble(L); }
  Section* section(int sy) { return secs[sy].get(); }
  const Section* section(int sy) const { return secs[sy].get(); }
  Section* ensure(int sy);
  void recomputeMaxY();
  void compact();                 // free all-air sections

  std::array<std::unique_ptr<Section>, SECS> secs;
  std::array<u8, SECS> airLight;  // nibble of unallocated sections
  std::array<LightRgb, SECS> airRgb;  // rgb of unallocated sections (open sky is full sun)
  std::unordered_map<int, json> blockEntities;   // colIdx -> {type, items:[...], ...} (chests, furnaces, piles, signs)
  std::array<u8, 512> climate{};  // temp, humidity per column (colour tints) — like JS chunk.climate
  u8 heightMap[256]{};            // highest non-air y per column (band-local), for skylight and LOD
  int maxY = 0;                   // highest non-air y + 1
  bool modified = false;          // player changes -> saved
  bool dirty = false;             // unsaved changes
  bool lightReady = false;
  i64 savedAt = 0;
  u32 sectionDirty = 0;           // bit per section needing a remesh
  std::atomic<int> refs{0};       // background readers (meshing gathers on the main thread, so rarely used)
  json extra;                     // per-chunk data for systems (structures placed, etc.)
};

// Save codec (palette of block keys + varint RLE, y-fastest) — same idea as the JS format v2.
std::vector<u8> encodeChunk(const Chunk& c);
bool decodeChunk(Chunk& c, const u8* data, size_t n);

} // namespace dq
