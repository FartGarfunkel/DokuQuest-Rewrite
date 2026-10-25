#include "gen/structures.h"
#include <algorithm>
#include <atomic>

namespace dq {

static std::vector<StructureType>& typeList() { static std::vector<StructureType> v; return v; }
int registerStructure(StructureType t) { typeList().push_back(std::move(t)); return (int)typeList().size() - 1; }
const std::vector<StructureType>& structureTypes() { return typeList(); }

// ---- ChunkWriter ----
int ChunkWriter::put(int x, int y, int z, BlockId id, u8 meta) {
  if (!in(x, z) || y < 1 || y >= DIM_H) return -1;
  int i = colIdx(x - bx, y, z - bz);
  buf.blocks[i] = id; buf.meta[i] = meta;
  ch.blockEntities.erase(i);
  return i;
}
BlockId ChunkWriter::get(int x, int y, int z) const {
  return in(x, z) && y >= 0 && y < DIM_H ? buf.blocks[colIdx(x - bx, y, z - bz)] : (BlockId)0xFFFF;
}
void ChunkWriter::fill(int x0, int y0, int z0, int x1, int y1, int z1, BlockId id, u8 meta) {
  int ax = std::max(x0, bx), ex = std::min(x1, bx + 15), az = std::max(z0, bz), ez = std::min(z1, bz + 15);
  for (int y = y0; y <= y1; y++) for (int z = az; z <= ez; z++) for (int x = ax; x <= ex; x++) put(x, y, z, id, meta);
}
void ChunkWriter::fill(int x0, int y0, int z0, int x1, int y1, int z1, const std::function<BlockId(int, int, int)>& f, u8 meta) {
  int ax = std::max(x0, bx), ex = std::min(x1, bx + 15), az = std::max(z0, bz), ez = std::min(z1, bz + 15);
  for (int y = y0; y <= y1; y++) for (int z = az; z <= ez; z++) for (int x = ax; x <= ex; x++) put(x, y, z, f(x, y, z), meta);
}
void ChunkWriter::blockEntity(int x, int y, int z, json be) {
  if (!in(x, z) || y < 0 || y >= DIM_H) return;
  ch.blockEntities[colIdx(x - bx, y, z - bz)] = std::move(be);
}

// ---- StructureIndex ----
static std::atomic<bool> gStructuresOn{true};
static std::atomic<u64> gIndexSerial{1};
void setStructuresEnabled(bool on) { gStructuresOn = on; }
int structureTypeIndex(const std::string& key) {
  const auto& all = structureTypes();
  for (int k = 0; k < (int)all.size(); k++) if (all[k].key == key) return k;
  return -1;
}

StructureIndex::StructureIndex(const TerrainQuery& t, int realm) : t(t), realm(realm), serial(gIndexSerial++) {
  if (!gStructuresOn) return;
  const auto& all = structureTypes();
  for (int k = 0; k < (int)all.size(); k++) {
    const StructureType& T = all[k];
    if (!(T.dims & (1u << t.dim())) || !(T.realms & (1u << realm)) || !T.build) continue;
    if (!T.plan && !T.place && !T.global) continue;
    types.push_back(k);
    if (T.flatten) anyFlatten = true;
  }
}

const std::vector<StructurePlan>& StructureIndex::globals(int k) const {
  {
    std::lock_guard<std::mutex> lk(mx);
    auto it = globalCache.find(k);
    if (it != globalCache.end()) return *it->second;
  }
  auto list = std::make_unique<std::vector<StructurePlan>>();
  structureTypes()[k].global(t, *list);
  for (auto& p : *list) { p.type = k; if (p.name.empty()) p.name = structureTypes()[k].name; }
  std::lock_guard<std::mutex> lk(mx);
  auto [it, fresh] = globalCache.emplace(k, std::move(list));
  return *it->second;
}

const StructurePlan* StructureIndex::at(int k, int rx, int rz) const {
  const StructureType& T = structureTypes()[k];
  if (T.global) return nullptr;
  u64 key = ((u64)(u32)k << 56) ^ ((u64)(u32)(rx & 0x0FFFFFFF) << 28) ^ (u64)(u32)(rz & 0x0FFFFFFF);
  {
    std::lock_guard<std::mutex> lk(mx);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second.get();
  }
  // worked out without the lock: plan() may ask about other structures (earlier types) itself
  std::unique_ptr<StructurePlan> S;
  auto p = std::make_unique<StructurePlan>();
  if (T.place) {
    if (T.place(*this, t, rx, rz, *p)) { p->type = k; if (p->name.empty()) p->name = T.name; S = std::move(p); }
  } else {
    const i32 seed = t.seed();
    const int ji = T.jsIndex >= 0 ? T.jsIndex : k, jd = jsFromDim(t.dim());
    Mulberry32 rnd(lairSeed(seed, rx, rz, 9100 + ji * 37 + jd));
    if (rnd() < T.chance) {
      int m = T.margin;
      int x = rx * T.region + m + jsFloor(rnd() * (T.region - 2 * m)), z = rz * T.region + m + jsFloor(rnd() * (T.region - 2 * m));
      if (T.plan(t, x, z, rnd, *p)) {
        p->type = k;
        p->id = "s" + std::to_string(ji) + "_" + std::to_string(jd) + "_" + std::to_string(rx) + "_" + std::to_string(rz);
        p->seed = lairSeed(seed, rx, rz, 9200 + ji);
        if (p->name.empty()) p->name = T.name;
        // keep clear of everything registered before it (structures.js structAt, in the original's float maths)
        const double cx = (p->x0 + p->x1) / 2.0, cz = (p->z0 + p->z1) / 2.0, rad = std::max(p->x1 - p->x0, p->z1 - p->z0) / 2.0;
        bool clear = true;
        for (int j : types) {
          if (j >= k || !clear) break;
          const StructureType& J = structureTypes()[j];
          const double pad = rad + J.clearance;
          auto hit = [&](const StructurePlan* o) { return o && cx >= o->x0 - pad && cx <= o->x1 + pad && cz >= o->z0 - pad && cz <= o->z1 + pad; };
          if (J.global) { for (const auto& o : globals(j)) if (hit(&o)) { clear = false; break; } continue; }
          const int qx = jsFloor(cx / J.region), qz = jsFloor(cz / J.region);
          for (int dz = -1; dz <= 1 && clear; dz++) for (int dx = -1; dx <= 1; dx++) if (hit(at(j, qx + dx, qz + dz))) { clear = false; break; }
        }
        if (clear) S = std::move(p);
      }
    }
  }
  std::lock_guard<std::mutex> lk(mx);
  auto [it, fresh] = cache.emplace(key, std::move(S));
  return it->second.get();
}

void StructureIndex::near(int x, int z, int pad, std::vector<const StructurePlan*>& out, int only) const {
  for (int k : types) {
    if (only >= 0 && k != only) continue;
    const StructureType& T = structureTypes()[k];
    if (T.global) {
      for (const auto& S : globals(k)) if (x >= S.x0 - pad && x <= S.x1 + pad && z >= S.z0 - pad && z <= S.z1 + pad) out.push_back(&S);
      continue;
    }
    int rx = floordiv(x, T.region), rz = floordiv(z, T.region);
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
      const StructurePlan* S = at(k, rx + dx, rz + dz);
      if (S && x >= S->x0 - pad && x <= S->x1 + pad && z >= S->z0 - pad && z <= S->z1 + pad) out.push_back(S);
    }
  }
}

// Every plan within 16 blocks of a chunk, remembered per thread for the chunk last asked about (column levelling
// and the tree filter ask once per column / tree).
const std::vector<const StructurePlan*>& StructureIndex::chunkPlans(int cx, int cz) const {
  struct Memo { u64 serial = 0; int cx = 0, cz = 0; std::vector<const StructurePlan*> list; };
  thread_local Memo memo;
  if (memo.serial == serial && memo.cx == cx && memo.cz == cz) return memo.list;
  std::vector<const StructurePlan*> list;
  near(cx * 16 + 8, cz * 16 + 8, 24, list);
  memo.serial = serial; memo.cx = cx; memo.cz = cz; memo.list = std::move(list);
  return memo.list;
}

void StructureIndex::build(GenBuf& buf, Chunk& c) const {
  if (types.empty()) return;
  int bx = c.cx * 16, bz = c.cz * 16;
  std::vector<const StructurePlan*> list;
  near(bx + 8, bz + 8, 12, list);
  for (const StructurePlan* S : list) {
    if (S->x1 < bx || S->x0 > bx + 15 || S->z1 < bz || S->z0 > bz + 15) continue;
    ChunkWriter w(buf, c, *S, t);
    structureTypes()[S->type].build(w, *S);
  }
}

bool StructureIndex::flatten(int x, int z, double& weight, int& targetY) const {
  if (!anyFlatten) return false;
  double best = 0; int ty = 0;
  for (const StructurePlan* S : chunkPlans(floordiv(x, 16), floordiv(z, 16))) {
    const StructureType& T = structureTypes()[S->type];
    if (!T.flatten || x < S->x0 - T.flattenPad || x > S->x1 + T.flattenPad || z < S->z0 - T.flattenPad || z > S->z1 + T.flattenPad) continue;
    double w = 0; int y = 0;
    if (T.flatten(*S, x, z, w, y) && w > best) { best = w; ty = y; }
  }
  if (best <= 0) return false;
  weight = best; targetY = ty;
  return true;
}

bool StructureIndex::flagged(int x, int z, int pad, u32 flag) const {
  if (types.empty()) return false;
  auto check = [&](const StructurePlan* S) {
    const StructureType& T = structureTypes()[S->type];
    if (flag == SF_SURFACE && T.treeOff) return x >= S->x0 - T.treePad && x <= S->x1 + T.treePad && z >= S->z0 - T.treePad && z <= S->z1 + T.treePad && T.treeOff(*S, x, z);
    const bool has = (T.flags & flag) || (flag == SF_SURFACE && S->surface);
    return has && x >= S->x0 - pad && x <= S->x1 + pad && z >= S->z0 - pad && z <= S->z1 + pad;
  };
  if (pad <= 12) { for (const StructurePlan* S : chunkPlans(floordiv(x, 16), floordiv(z, 16))) if (check(S)) return true; return false; }
  std::vector<const StructurePlan*> list;
  near(x, z, pad + 4, list);
  for (const StructurePlan* S : list) if (check(S)) return true;
  return false;
}

const StructurePlan* StructureIndex::nearest(int k, int x, int z, int maxRegions) const {
  const StructureType& T = structureTypes()[k];
  if (std::find(types.begin(), types.end(), k) == types.end()) return nullptr;
  const StructurePlan* best = nullptr; double bd = 1e300;
  auto consider = [&](const StructurePlan* S) {
    if (!S) return;
    double d = std::hypot((S->x0 + S->x1) / 2.0 - x, (S->z0 + S->z1) / 2.0 - z);
    if (d < bd) { bd = d; best = S; }
  };
  if (T.global) { for (const auto& S : globals(k)) consider(&S); return best; }
  const int rx = floordiv(x, T.region), rz = floordiv(z, T.region);
  for (int r = 0; r <= maxRegions; r++) {
    if (best && (r - 1) * (double)T.region > bd) break;
    for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
      if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
      consider(at(k, rx + dx, rz + dz));
    }
  }
  return best;
}

} // namespace dq
