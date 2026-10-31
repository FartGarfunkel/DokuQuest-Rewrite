#include "world/world.h"
#include "world/carve_forms.h"
#include "world/region.h"
#include "world/light.h"
#include "world/shapes.h"

namespace dq {

World::World(int realm, std::shared_ptr<WorldGen> gen, const fs::path& saveDir, ThreadPool* pool)
  : realm(realm), gen(std::move(gen)), saveDir(saveDir), pool(pool) {
  store = std::make_shared<RegionStore>(saveDir);
  shared = std::make_shared<Shared>();
  shared->store = store; shared->gen = this->gen;
  initBlockTraits();
  addSystem(std::make_shared<LightEngine>());   // sky + block light across chunk/band borders, incremental (world/light.h)
}
World::~World() { flush(); }

Chunk* World::chunk(int dim, int cx, int cz) const {
  if (lastChunk && lastChunk->cx == cx && lastChunk->cz == cz && lastChunk->dim == dim) return lastChunk;
  auto it = map.find({dim, cx, cz});
  if (it == map.end()) return nullptr;
  lastChunk = it->second.get();
  return lastChunk;
}

BlockId World::getBlock(int x, int y, int z) const {
  if (!inWorldY(y)) return AIR;
  Chunk* c = chunk(y >> 8, x >> 4, z >> 4);
  return c ? c->get(x & 15, y & 255, z & 15) : AIR;
}
BlockId World::getBlockPhys(int x, int y, int z) const {
  if (y < 0) return AIR;            // the void under the End
  if (y >= WORLD_H) return AIR;
  Chunk* c = chunk(y >> 8, x >> 4, z >> 4);
  if (!c) return reg().blockId("stone");
  return c->get(x & 15, y & 255, z & 15);
}
u8 World::getMeta(int x, int y, int z) const {
  if (!inWorldY(y)) return 0;
  Chunk* c = chunk(y >> 8, x >> 4, z >> 4);
  return c ? c->getMeta(x & 15, y & 255, z & 15) : 0;
}
u8 World::getCarve(int x, int y, int z) const {
  if (!inWorldY(y)) return 0;
  Chunk* c = chunk(y >> 8, x >> 4, z >> 4);
  return c ? c->getCarve(x & 15, y & 255, z & 15) : 0;
}
bool World::setCarve(int x, int y, int z, u8 carve, u32 flags) {
  if (!inWorldY(y)) return false;
  Chunk* c = chunk(y >> 8, x >> 4, z >> 4);
  if (!c) return false;
  c->setCarve(x & 15, y & 255, z & 15, carve);
  markBlockDirty(x, y, z, flags & SetFlags::Urgent);
  return true;
}
u8 World::getLight(int x, int y, int z) const {
  if (!inWorldY(y)) return 0xF0;
  Chunk* c = chunk(y >> 8, x >> 4, z >> 4);
  return c ? c->getLight(x & 15, y & 255, z & 15) : 0xF0;
}
int World::surfaceY(int x, int z, int dim) const {
  Chunk* c = chunk(dim, x >> 4, z >> 4);
  if (!c) return -1;
  int ly = c->heightMap[(z & 15) * 16 + (x & 15)];
  if (ly == 0 && c->get(x & 15, 0, z & 15) == AIR) return -1;
  return dimBase(dim) + ly;
}

bool World::setBlock(int x, int y, int z, BlockId id, u8 meta, u32 flags) {
  if (!inWorldY(y)) return false;
  Chunk* c = chunk(y >> 8, x >> 4, z >> 4);
  if (!c) return false;
  int lx = x & 15, ly = y & 255, lz = z & 15;
  BlockId old = c->get(lx, ly, lz);
  u8 oldMeta = c->getMeta(lx, ly, lz);
  u8 setCarveTo = 0;
  if (isSlabOrStairs(id)) { CarveForm f = carveFormOf(id, meta); if (f.base) { id = f.base; meta = 0; setCarveTo = f.carve; } }   // placed or built: its chiseled form
  bool oldCarved = c->getCarve(lx, ly, lz);
  if (old == id && oldMeta == meta && !oldCarved) return false;   // a carved block must fall through: set() clears its carve byte
  // carve-bit trace is now off: the layout moved to a full carve byte (meta&0x7F) on plain blocks,
  // which share bits with facing states on shape blocks, so an unconditional gate would be noisy.
  c->set(lx, ly, lz, id, meta);
  if (setCarveTo) c->setCarve(lx, ly, lz, setCarveTo);
  if (old != id) { auto it = c->blockEntities.find(colIdx(lx, ly, lz)); if (it != c->blockEntities.end() && !(flags & SetFlags::NoNotify)) c->blockEntities.erase(it); }
  if (!(flags & SetFlags::NoModify)) { c->modified = true; c->dirty = true; }
  if (!(flags & SetFlags::NoRemesh)) markBlockDirty(x, y, z, flags & SetFlags::Urgent);
  if (!(flags & SetFlags::NoNotify)) { u32 pf = changeFlags; changeFlags = flags; for (auto& s : systems) s->onBlockChanged(*this, x, y, z, old, id); changeFlags = pf; }
  return true;
}
bool World::setMeta(int x, int y, int z, u8 meta, u32 flags) {
  return setBlock(x, y, z, getBlock(x, y, z), meta, flags);
}
json* World::blockEntity(int x, int y, int z, bool create) {
  Chunk* c = chunkAtBlock(x, y, z);
  if (!c) return nullptr;
  int i = colIdx(x & 15, y & 255, z & 15);
  auto it = c->blockEntities.find(i);
  if (it != c->blockEntities.end()) return &it->second;
  if (!create) return nullptr;
  c->modified = c->dirty = true;
  return &(c->blockEntities[i] = json::object());
}
void World::setBlockEntity(int x, int y, int z, json e) {
  Chunk* c = chunkAtBlock(x, y, z);
  if (!c) return;
  int i = colIdx(x & 15, y & 255, z & 15);
  if (e.is_null()) c->blockEntities.erase(i); else c->blockEntities[i] = std::move(e);
  c->modified = c->dirty = true;
  for (auto& s : systems) s->onBlockEntityChanged(*this, x, y, z);
}
void World::blockEntityChanged(int x, int y, int z) {
  markModified(x, y, z);
  for (auto& s : systems) s->onBlockEntityChanged(*this, x, y, z);
}
void World::markModified(int x, int y, int z) { if (Chunk* c = chunkAtBlock(x, y, z)) c->modified = c->dirty = true; }

void World::markSectionDirty(int dim, int cx, int sy, int cz, bool urgent) {
  if (sy < 0) { if (dim == 0) return; dim--; sy += SECS; }
  if (sy >= SECS) { if (dim >= DIM_COUNT - 1) return; dim++; sy -= SECS; }
  SectionKey k{dim, cx, sy, cz};
  if (urgent) urgentSecs.push_back(k); else dirtySecs.insert(k);
}
void World::markBlockDirty(int x, int y, int z, bool urgent) {
  int d = y >> 8, cx = x >> 4, cz = z >> 4, sy = (y & 255) >> 4;
  markSectionDirty(d, cx, sy, cz, urgent);
  int lx = x & 15, ly = y & 15, lz = z & 15;
  if (lx == 0) markSectionDirty(d, cx - 1, sy, cz, urgent);
  if (lx == 15) markSectionDirty(d, cx + 1, sy, cz, urgent);
  if (lz == 0) markSectionDirty(d, cx, sy, cz - 1, urgent);
  if (lz == 15) markSectionDirty(d, cx, sy, cz + 1, urgent);
  if (ly == 0) markSectionDirty(d, cx, sy - 1, cz, urgent);
  if (ly == 15) markSectionDirty(d, cx, sy + 1, cz, urgent);
}
std::vector<SectionKey> World::drainDirtySections() {
  std::vector<SectionKey> out = std::move(urgentSecs);
  urgentSecs.clear();
  out.insert(out.end(), dirtySecs.begin(), dirtySecs.end());
  dirtySecs.clear();
  return out;
}
std::vector<ChunkKey> World::drainLoaded() { auto v = std::move(loadedQ); loadedQ.clear(); return v; }
std::vector<ChunkKey> World::drainUnloaded() { auto v = std::move(unloadedQ); unloadedQ.clear(); return v; }

bool World::hasNeighbours(const Chunk& c) const {
  for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) if ((dx || dz) && !chunk(c.dim, c.cx + dx, c.cz + dz)) return false;
  return true;
}

void World::requestChunk(int dim, int cx, int cz, float priority) {
  ChunkKey k{dim, cx, cz};
  if (map.count(k) || inFlight.count(k)) return;
  inFlight.insert(k);
  auto sh = shared;
  auto job = [sh, k] {
    auto c = std::make_unique<Chunk>(k.dim, k.cx, k.cz);
    std::vector<u8> blob;
    bool loaded = sh->store->load(k, blob) && decodeChunk(*c, blob.data(), blob.size());
    if (!loaded) {
      if (DimGenerator* g = sh->gen ? sh->gen->forDim(k.dim) : nullptr) g->generate(*c);
      c->modified = false;
    }
    convertSlabsStairs(*c);   // slabs and stairs arrive as chiseled blocks (generation and old saves)
    c->recomputeMaxY();
    initialChunkLight(*c);
    c->compact();
    if (loaded) c->extra["_loaded"] = true;
    std::lock_guard<std::mutex> lk(sh->mx);
    sh->done.push_back(std::move(c));
  };
  if (pool) pool->submit(job, priority); else job();
}

void World::insertChunk(std::unique_ptr<Chunk> c) {
  ChunkKey k = c->key();
  inFlight.erase(k);
  if (map.count(k)) return;
  bool wasLoaded = c->extra.is_object() && c->extra.contains("_loaded");
  if (wasLoaded) c->extra.erase("_loaded");
  Chunk* raw = c.get();
  map[k] = std::move(c);
  lastChunk = nullptr;
  if (!wasLoaded && !(raw->extra.is_object() && raw->extra.value("populated", false))) needPopulate.insert(k);
  for (auto& s : systems) s->onChunkLoaded(*this, *raw);
  for (int sy = 0; sy < SECS; sy++) markSectionDirty(k.dim, k.cx, sy, k.cz);
  // neighbours' edge faces can change now
  for (int sy = 0; sy < SECS; sy++) {
    if (chunk(k.dim, k.cx - 1, k.cz)) markSectionDirty(k.dim, k.cx - 1, sy, k.cz);
    if (chunk(k.dim, k.cx + 1, k.cz)) markSectionDirty(k.dim, k.cx + 1, sy, k.cz);
    if (chunk(k.dim, k.cx, k.cz - 1)) markSectionDirty(k.dim, k.cx, sy, k.cz - 1);
    if (chunk(k.dim, k.cx, k.cz + 1)) markSectionDirty(k.dim, k.cx, sy, k.cz + 1);
  }
  loadedQ.push_back(k);
  // populate this chunk and any neighbour that just became surrounded
  for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
    ChunkKey nk{k.dim, k.cx + dx, k.cz + dz};
    if (needPopulate.count(nk)) if (Chunk* n = chunk(nk.dim, nk.cx, nk.cz)) tryPopulate(*n);
  }
}

void World::tryPopulate(Chunk& c) {
  if (!hasNeighbours(c)) return;
  needPopulate.erase(c.key());
  if (DimGenerator* g = gen ? gen->forDim(c.dim) : nullptr) g->populate(*this, c);
  if (!c.extra.is_object()) c.extra = json::object();
  c.extra["populated"] = true;
}

void World::unloadChunk(const ChunkKey& k, bool save) {
  auto it = map.find(k);
  if (it == map.end()) return;
  Chunk& c = *it->second;
  for (auto& s : systems) s->onChunkUnloading(*this, c);
  if (save && c.modified && c.dirty) { c.savedAt = unixMillis(); store->store(k, encodeChunk(c)); }
  needPopulate.erase(k);
  unloadedQ.push_back(k);
  lastChunk = nullptr;
  map.erase(it);
}

void World::stream(const std::vector<StreamAnchor>& anchors, double budgetMs) {
  // wanted set: for each anchor, its band (and the neighbouring band when near the edge)
  struct Want { int dim, cx, cz; float pri; };
  std::vector<Want> want;
  std::unordered_set<ChunkKey, ChunkKeyHash> keep;
  for (auto& a : anchors) {
    int y = (int)std::floor(a.pos.y);
    int d = dimOfY(clampv(y, 0, WORLD_H - 1));
    int pcx = ifloor(a.pos.x) >> 4, pcz = ifloor(a.pos.z) >> 4;
    std::vector<std::pair<int, int>> bands = {{d, a.radius}};
    if (a.nearDims) {
      int ly = y - dimBase(d);
      if (ly > DIM_H - 64 && d + 1 < DIM_COUNT) bands.push_back({d + 1, std::max(2, a.radius / 2)});
      if (ly < 48 && d > 0) bands.push_back({d - 1, std::max(2, a.radius / 2)});
    }
    for (auto& [bd, R] : bands) {
      for (int dz = -R - 1; dz <= R + 1; dz++) for (int dx = -R - 1; dx <= R + 1; dx++) {
        int d2 = dx * dx + dz * dz;
        if (d2 > (R + 1) * (R + 1)) continue;
        ChunkKey k{bd, pcx + dx, pcz + dz};
        keep.insert(k);
        if (d2 <= R * R + 1 && !map.count(k)) want.push_back({bd, k.cx, k.cz, (float)d2 + (bd == d ? 0 : 64)});
      }
    }
  }
  std::sort(want.begin(), want.end(), [](const Want& a, const Want& b) { return a.pri < b.pri; });
  int maxInFlight = pool ? pool->threads() * 4 + 4 : 2;
  for (auto& w : want) {
    if ((int)inFlight.size() >= maxInFlight) break;
    requestChunk(w.dim, w.cx, w.cz, w.pri);
  }
  pumpMain(budgetMs);
  // unload what no anchor needs (with hysteresis: keep includes radius+1)
  std::vector<ChunkKey> drop;
  for (auto& [k, c] : map) if (!keep.count(k)) {
    bool nearAny = false;
    for (auto& a : anchors) {
      int pcx = ifloor(a.pos.x) >> 4, pcz = ifloor(a.pos.z) >> 4;
      int dx = k.cx - pcx, dz = k.cz - pcz;
      if (dx * dx + dz * dz <= (a.radius + 3) * (a.radius + 3) && std::abs(k.dim - dimOfY((int)a.pos.y)) <= 1) { nearAny = true; break; }
    }
    if (!nearAny) drop.push_back(k);
  }
  for (auto& k : drop) unloadChunk(k, true);
}

void World::pumpMain(double budgetMs) {
  std::vector<std::unique_ptr<Chunk>> done;
  { std::lock_guard<std::mutex> lk(shared->mx); done.swap(shared->done); }
  double t0 = nowSeconds();
  size_t i = 0;
  for (; i < done.size(); i++) {
    insertChunk(std::move(done[i]));
    if ((nowSeconds() - t0) * 1000 > budgetMs) { i++; break; }
  }
  if (i < done.size()) {   // over budget: put the rest back for next frame
    std::lock_guard<std::mutex> lk(shared->mx);
    for (; i < done.size(); i++) shared->done.push_back(std::move(done[i]));
  }
  mainQueue.run(std::max(0.5, budgetMs - (nowSeconds() - t0) * 1000));
}

void World::unloadAll(bool save) {
  std::vector<ChunkKey> keys;
  for (auto& [k, c] : map) keys.push_back(k);
  for (auto& k : keys) unloadChunk(k, save);
  if (save) store->flush();
}

void World::saveDirty(bool all) {
  for (auto& [k, c] : map) if (c->modified && c->dirty) {
    c->dirty = false; c->savedAt = unixMillis();
    store->store(k, encodeChunk(*c));
  }
  if (all || !pool) store->flush();
  else { auto s = store; pool->submit([s] { s->flush(); }, -1); }
}
void World::flush() { if (store) store->flush(); }

} // namespace dq
