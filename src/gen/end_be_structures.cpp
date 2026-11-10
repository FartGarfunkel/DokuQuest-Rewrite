// gen/end_be_structures.cpp — Better End's land structures (its building_list_features: ruins, houses, a library, a
// tree house, a small mansion, cabins, stumps and fallen trees) in their six lands, read from data/be_structures.json
// (tools/port/be_building.py converts Better End's MIT .nbt templates). Like Better End: in one chunk of ten (its rarity
// filter) a spot in the chunk, if its land has a list, gets one structure from it, centred there, turned a random
// quarter; SURFACE ones sit with their floor in the ground, OBJECT ones on top of it. Its air clears only what stands
// above the ground.
#include "gen/dimgens.h"
#include "gen/structures.h"
#include "common/fs.h"
#include "core/registry.h"
#include <array>

namespace dq {

namespace {
struct BEStruct {
  bool object = false; int offY = 0, sx = 0, sy = 0, sz = 0;
  std::vector<BlockId> id; std::vector<std::array<u8, 4>> meta;
  std::vector<u32> blocks;   // x | y << 8 | z << 16 | palette << 24
};
struct BELand { int chance = 0; std::vector<BEStruct> list; };
BEStruct readStruct(const json& s) {
  BEStruct S; S.object = s.value("merger", "SURFACE") == "OBJECT"; S.offY = s.value("offY", 0);
  S.sx = s["size"][0]; S.sy = s["size"][1]; S.sz = s["size"][2];
  for (auto& p : s["palette"]) {
    const std::string key = p[0];
    S.id.push_back(key == "air" ? AIR : reg().blockId(key));
    S.meta.push_back({(u8)p[1].get<int>(), (u8)p[2].get<int>(), (u8)p[3].get<int>(), (u8)p[4].get<int>()});
  }
  S.blocks = s["blocks"].get<std::vector<u32>>();
  return S;
}
// Better End's pillar bases and tops (data/be_structures.json "pillars", from its pillars/*.nbt)
const std::unordered_map<std::string, BEStruct>& bePillarTpls() {
  static const std::unordered_map<std::string, BEStruct> M = [] {
    std::unordered_map<std::string, BEStruct> out;
    auto j = readJson(dataDir() / "be_structures.json");
    if (j && (*j)["pillars"].is_object()) for (auto& [k, v] : (*j)["pillars"].items()) out[k] = readStruct(v);
    return out;
  }();
  return M;
}
const BEStruct* bePillarTpl(const std::string& name) { auto& M = bePillarTpls(); auto it = M.find(name); return it == M.end() ? nullptr : &it->second; }
void putStruct(const BEStruct& S, int x0, int y0, int z0, ChunkWriter& P) {   // unturned, air included (like a template placed in the world)
  for (u32 b : S.blocks) {
    const int x = x0 + (int)(b & 255), y = y0 + (int)((b >> 8) & 255), z = z0 + (int)((b >> 16) & 255), pi = (int)(b >> 24);
    if (!P.in(x, z) || y < 1 || y >= DIM_H) continue;
    P.put(x, y, z, S.id[pi], S.meta[pi][0]);
  }
}
const std::array<BELand, 19>& beStructs() {
  static const std::array<BELand, 19> L = [] {
    std::array<BELand, 19> out;
    auto j = readJson(dataDir() / "be_structures.json");
    if (!j || !(*j)["lands"].is_object()) { DQ_WARN("End: no data/be_structures.json, Better End's buildings are off"); return out; }
    for (auto& [k, v] : (*j)["lands"].items()) {
      const int land = std::atoi(k.c_str());
      if (land < 0 || land >= 19) continue;
      BELand& BL = out[land]; BL.chance = std::max(1, v.value("chance", 10));
      for (auto& s : v["list"]) BL.list.push_back(readStruct(s));
    }
    return out;
  }();
  return L;
}
}

// SpikeFeatureMixin.be_placeSpike: a pillar of radius 2-5 becomes Better End's base (3 into the ground) and top piece
// (the caged one for the guarded pillars), the obsidian between them one narrower, with crying obsidian on its rim now
// and then (1 in 24). Its height counts from the ground: top = ground + vanilla height - 64. The crystal stands on the
// top's bedrock (sim/end_fight.cpp finds it there).
bool EndGen::bePillar(const Pillar& pl, ChunkWriter* P, int* topY) const {
  if (!betterEnd() || pl.rad < 2 || pl.rad > 5) return false;
  const BEStruct* base = bePillarTpl("pillar_base_" + std::to_string(pl.rad - 1));
  const BEStruct* top = bePillarTpl("pillar_top_" + std::to_string(pl.rad - 1) + (pl.cage ? "_cage" : ""));
  if (!base || !top) return false;
  Isle c;
  int minY = (island(pl.x, pl.z, c) ? c.top : 60 + vmap::END_DY) + 1;   // the first open block over the ground
  const int maxY = minY + (pl.h - vmap::END_DY) - 64, baseY = minY - 3;
  minY = baseY + base->sy;
  if (topY) *topY = maxY + top->sy - 1;
  if (!P) return true;
  putStruct(*base, pl.x - (base->sx >> 1), baseY, pl.z - (base->sz >> 1), *P);
  putStruct(*top, pl.x - (top->sx >> 1), maxY, pl.z - (top->sz >> 1), *P);
  static const BlockId obsidian = reg().blockId("obsidian"), crying = reg().blockId("crying_obsidian");
  const Registry& R = reg();
  const int r = pl.rad - 1, r2 = r * r + 1;
  for (int pz = -r; pz <= r; pz++) for (int px = -r; px <= r; px++) {
    const int x = pl.x + px, z = pl.z + pz;
    if (px * px + pz * pz > r2 || !P->in(x, z)) continue;
    const bool rim = px == r || px == -r || pz == r || pz == -r;
    for (int y = std::max(1, minY); y < maxY && y < DIM_H; y++) {
      const BlockId was = P->get(x, y, z);
      if (was != AIR && !R.tLiquid[was]) continue;   // only into the open, like the mod
      P->put(x, y, z, rim && crying && hash32(x, y, z, seed_ ^ 0x5B1E) % 24 == 0 ? crying : obsidian);
    }
  }
  return true;
}

void EndGen::beStructures(int bx, int bz, ChunkWriter& P) const {
  const auto& L = beStructs();
  const int cx = floordiv(bx, 16), cz = floordiv(bz, 16);
  for (int oz = cz - 2; oz <= cz + 2; oz++) for (int ox = cx - 2; ox <= cx + 2; ox++) {
    const u32 h = hash32(ox, oz, seed_, 0xBE57);
    if ((h & 0xFFFF) % 10 != 0) continue;   // Better End's rarity filter (1 in 10 for every land's list)
    const int x = ox * 16 + (int)((h >> 16) & 15), z = oz * 16 + (int)((h >> 20) & 15);
    if (std::hypot((double)x, (double)z) < OUTER + 24) continue;
    Isle c; if (!island(x, z, c) || c.main) continue;
    const int land = endBiome(x, z);
    if (land < 0 || land >= 19 || L[land].list.empty()) continue;
    const BEStruct& S = L[land].list[(h >> 24) % L[land].list.size()];
    const int rot = (int)((h >> 28) & 3);
    const int rsx = rot & 1 ? S.sz : S.sx, rsz = rot & 1 ? S.sx : S.sz;
    const int x0 = x - rsx / 2, z0 = z - rsz / 2, y0 = c.top + (S.object ? 1 : 0) + S.offY;
    if (x0 > bx + 15 || x0 + rsx <= bx || z0 > bz + 15 || z0 + rsz <= bz) continue;
    for (u32 b : S.blocks) {
      const int lx = b & 255, ly = (b >> 8) & 255, lz = (b >> 16) & 255, pi = (int)(b >> 24);
      int rx, rz;
      switch (rot) { case 1: rx = S.sz - 1 - lz; rz = lx; break; case 2: rx = S.sx - 1 - lx; rz = S.sz - 1 - lz; break; case 3: rx = lz; rz = S.sx - 1 - lx; break; default: rx = lx; rz = lz; }
      const int wx = x0 + rx, wz = z0 + rz, y = y0 + ly;
      if (!P.in(wx, wz) || y < 1 || y >= DIM_H) continue;
      const BlockId id = S.id[pi];
      if (id == AIR) { if (y > c.top) P.put(wx, y, wz, AIR); continue; }
      if (!id) continue;
      P.put(wx, y, wz, id, S.meta[pi][rot]);
    }
  }
}

} // namespace dq
