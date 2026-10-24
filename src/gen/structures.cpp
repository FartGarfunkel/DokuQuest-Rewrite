#include "gen/structures.h"

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
StructureIndex::StructureIndex(const TerrainQuery& t, int realm) : t(t), realm(realm) {
  const auto& all = structureTypes();
  for (int k = 0; k < (int)all.size(); k++) {
    const StructureType& T = all[k];
    if (!(T.dims & (1u << t.dim())) || !(T.realms & (1u << realm)) || !T.plan || !T.build) continue;
    types.push_back(k);
    if (T.flatten) anyFlatten = true;
  }
}

const StructurePlan* StructureIndex::at(int k, int rx, int rz) const {
  const StructureType& T = structureTypes()[k];
  u64 key = ((u64)(u32)k << 56) ^ ((u64)(u32)(rx & 0x0FFFFFFF) << 28) ^ (u64)(u32)(rz & 0x0FFFFFFF);
  {
    std::lock_guard<std::mutex> lk(mx);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second.get();
  }
  // worked out without the lock: plan() may ask about other structures (earlier types) itself
  std::unique_ptr<StructurePlan> S;
  i32 seed = t.seed();
  Mulberry32 rnd(lairSeed(seed, rx, rz, 9100 + k * 37 + jsFromDim(t.dim())));
  if (rnd() < T.chance) {
    int m = T.margin;
    int x = rx * T.region + m + jsFloor(rnd() * (T.region - 2 * m)), z = rz * T.region + m + jsFloor(rnd() * (T.region - 2 * m));
    auto p = std::make_unique<StructurePlan>();
    if (T.plan(t, x, z, rnd, *p)) {
      p->type = k;
      p->id = "s" + std::to_string(k) + "_" + std::to_string(jsFromDim(t.dim())) + "_" + std::to_string(rx) + "_" + std::to_string(rz);
      p->seed = lairSeed(seed, rx, rz, 9200 + k);
      if (p->name.empty()) p->name = T.name;
      // keep clear of everything registered before it
      int cx = (p->x0 + p->x1) / 2, cz = (p->z0 + p->z1) / 2, rad = std::max(p->x1 - p->x0, p->z1 - p->z0) / 2;
      bool clear = true;
      std::vector<const StructurePlan*> hits;
      for (int j : types) {
        if (j >= k) break;
        hits.clear(); near(cx, cz, rad + structureTypes()[j].clearance, hits, j);
        if (!hits.empty()) { clear = false; break; }
      }
      if (clear) S = std::move(p);
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
    int rx = floordiv(x, T.region), rz = floordiv(z, T.region);
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
      const StructurePlan* S = at(k, rx + dx, rz + dz);
      if (S && x >= S->x0 - pad && x <= S->x1 + pad && z >= S->z0 - pad && z <= S->z1 + pad) out.push_back(S);
    }
  }
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
  std::vector<const StructurePlan*> list;
  for (int k : types) {
    const StructureType& T = structureTypes()[k];
    if (!T.flatten) continue;
    list.clear(); near(x, z, T.flattenPad, list, k);
    for (const StructurePlan* S : list) { double w = 0; int y = 0; if (T.flatten(*S, x, z, w, y) && w > best) { best = w; ty = y; } }
  }
  if (best <= 0) return false;
  weight = best; targetY = ty;
  return true;
}

bool StructureIndex::flagged(int x, int z, int pad, u32 flag) const {
  if (types.empty()) return false;
  std::vector<const StructurePlan*> list;
  for (int k : types) {
    if (!(structureTypes()[k].flags & flag)) continue;
    list.clear(); near(x, z, pad, list, k);
    if (!list.empty()) return true;
  }
  return false;
}

} // namespace dq
