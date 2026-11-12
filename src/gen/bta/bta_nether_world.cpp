// The decoration world + the BTA block logic side effects that the Nether decorator triggers.
// Ported from World / Chunk / NeighborUpdates and BlockLogicFluid{,Still,Flowing}, FluidHardening,
// BlockLogicFire, BlockLogicMagma/Brimthaw, BlockLogicSulfur, BlockLogicThermalVent,
// BlockLogicGrowthRubyglass, BlockLogicFlower (bone pile/boulders/soul catcher), BlockLogicLayerSupportable.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif
#include "bta_nether_world.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace bta {

namespace {
// Direction offsets: DOWN, UP, NORTH, SOUTH, WEST, EAST
const int DX[6] = {0, 0, 0, 0, -1, 1}, DY[6] = {-1, 1, 0, 0, 0, 0}, DZ[6] = {0, 0, -1, 1, 0, 0};
enum { D_DOWN = 0, D_UP, D_NORTH, D_SOUTH, D_WEST, D_EAST };
inline bool isFluid(uint16_t id) { return hasF(id, P_FLUID_LOGIC); }
inline bool solidRender(uint16_t id) { return hasF(id, P_SOLID_RENDER); }
inline uint64_t tickKey(int x, int y, int z, uint16_t b) {
  return ((uint64_t)(uint32_t)(x & 0x3FFFFF) << 42) | ((uint64_t)(uint32_t)(z & 0x3FFFFF) << 20) |
         ((uint64_t)(uint32_t)(y & 0xFF) << 12) | (uint64_t)(b & 0xFFF);
}
void fizz(DecoWorld& w) { w.wrand.nextFloat(); w.wrand.nextFloat(); }  // sound pitch (world.rand)
}  // namespace

#ifdef BTA_DEBUG_TRACE
bool traceEnabled() { static const bool on = getenv("LOGSET") != nullptr; return on; }
void DecoWorld::trace(char c, int x, int y, int z, int id, int d) const {
  if (!traceEnabled()) return;
  if (c == 'S' || c == 'N') printf("%c %d %d %d %d\n", c, x, y, z, id);
  else if (id < 0) printf("%c %d %d %d %d\n", c, x, y, z, d);
  else printf("%c %d %d %d %d %d\n", c, x, y, z, id, d);
}
#endif
bool DecoWorld::normalCube(int x, int y, int z) const { return solidRender(get(x, y, z)); }

int DecoWorld::biome(int x, int y, int z) const {
  if (!inBounds(x, y, z)) return 255;  // Biomes.OVERWORLD_PLAINS
  if (y < 64) return BI_OLD_WORLD_DESERT;
  // stored chunk biome (loaded chunk) == BiomeProviderNether.getBiome (unloaded): same climate either way
  int s = slot(x, z);
  if (s >= 0 && slots[s]) return slots[s]->climate[((z & 15) << 4) | (x & 15)];
  return g->climateAt(x, z, nullptr);
}

void DecoWorld::load(int s) const {
  auto d = std::make_unique<Slot>();
  int cx = ox + s % nx, cz = oz + s / nx;
  auto t = g->terrain(cx, cz);
  for (int i = 0; i < 65536; ++i) d->ids[i] = nbToBtaId((NB)t->kind[i]);
  for (int sec = 0; sec < 16; ++sec) d->alloc[sec] = (t->sections >> sec) & 1;
  std::memcpy(d->climate, t->climate, 256);
  slots[s] = std::move(d);
}

bool DecoWorld::setIdData(int x, int y, int z, uint16_t id, int d, bool raw) {
  if (!inBounds(x, y, z)) return false;
  int s = slot(x, z);
  if (s < 0) return true;  // EmptyChunk.setBlockId/setBlockIdData: returns true, stores nothing
  Slot& sd = slotData(s);
  int i = lidx(x, y, z);
  uint8_t& al = sd.alloc[y >> 4];
  uint16_t cur = sd.ids[i];
  int curData = al ? sd.getMeta(i) : 0;
  if (cur == id && curData == (d & 0xFF)) return false;
  // ChunkSection.setBlockId: ignored for AIR into an unallocated section
  if (!(al == 0 && id == 0)) {
    al = 1;
    sd.ids[i] = id;
  }
  if (al) sd.setMeta(i, d & 0xFF);
  (void)raw;
  // currentBlock.onRemoved(...) : no generation-visible effect for any Nether block
  if (!raw) blockOnPlaced(*this, x, y, z, id);
  return true;
}

bool DecoWorld::setBlockDataRaw(int x, int y, int z, int d) {
  if (!inBounds(x, y, z)) return false;
  int s = slot(x, z);
  if (s < 0) return true;
  Slot& sd = slotData(s);
  if (!sd.alloc[y >> 4]) return true;  // ChunkSection.setData ignored when blocks == null
  sd.setMeta(lidx(x, y, z), d & 0xFF);
  return true;
}

void DecoWorld::notifyNeighbors(int x, int y, int z, uint16_t src) {
  // D1_OFFSETS = {WEST, EAST, DOWN, UP, NORTH, SOUTH}
  static const int ord[6] = {D_WEST, D_EAST, D_DOWN, D_UP, D_NORTH, D_SOUTH};
  for (int k = 0; k < 6; ++k) {
    int d = ord[k];
    int qx = x + DX[d], qy = y + DY[d], qz = z + DZ[d];
    blockOnNeighborChanged(*this, qx, qy, qz, get(qx, qy, qz), src);
  }
}

void DecoWorld::scheduleUpdate(int x, int y, int z, uint16_t block) {
  if (!immediate) return;  // queued in tickNextTickList: never processed during generation
  uint64_t k = tickKey(x, y, z, block);
  if (immSet.count(k)) return;
  if (areBlocksLoaded(x, z, 8) && get(x, y, z) == block) {
    immSet.insert(k);
    ++tickDepth;
    blockUpdateTick(*this, x, y, z, block, wrand);
    --tickDepth;
  }
}

// ---- fluids ------------------------------------------------------------------------------------
namespace {
enum FluidKind { FK_NONE, FK_WATER, FK_LAVA, FK_ACID };
inline FluidKind fluidKind(uint16_t id) {
  switch (id) {
    case bid::WATER_FLOWING: case bid::WATER_STILL: return FK_WATER;
    case bid::LAVA_FLOWING: case bid::LAVA_STILL: return FK_LAVA;
    case bid::ACID_FLOWING: case bid::ACID_STILL: return FK_ACID;
    default: return FK_NONE;
  }
}
inline Mat fluidMat(FluidKind k) { return k == FK_WATER ? Mat::WATER : k == FK_LAVA ? Mat::LAVA : Mat::ACID; }
inline uint16_t stillOf(FluidKind k) { return k == FK_WATER ? bid::WATER_STILL : k == FK_LAVA ? bid::LAVA_STILL : bid::ACID_STILL; }
inline uint16_t flowingOf(FluidKind k) { return k == FK_WATER ? bid::WATER_FLOWING : k == FK_LAVA ? bid::LAVA_FLOWING : bid::ACID_FLOWING; }
inline bool isFlowingId(uint16_t id) { return id == bid::WATER_FLOWING || id == bid::LAVA_FLOWING || id == bid::ACID_FLOWING; }

bool fluidHarden(DecoWorld& w, FluidKind k, int x, int y, int z, Mat enc) {
  int d = w.data(x, y, z) & 0xF;
  if (k == FK_WATER) {
    if (enc == Mat::LAVA) {
      if (w.mat(x, y + 1, z) != Mat::LAVA) return false;
      w.setBlockTypeNotify(x, y, z, d == 0 ? bid::OBSIDIAN : bid::COBBLE_GLOOMSTONE);
      fizz(w);
      return true;
    }
    if (enc == Mat::ACID) {
      if (w.mat(x, y + 1, z) != Mat::ACID) return false;
      w.setBlockTypeNotify(x, y, z, d == 0 ? bid::OBSIDIAN : bid::GLOOMSTONE);
      fizz(w);
      return true;
    }
    return false;
  }
  if (k == FK_LAVA) {
    if (enc == Mat::WATER) { w.setBlockTypeNotify(x, y, z, d == 0 ? bid::MAGMA : bid::COBBLE_NETHERRACK); fizz(w); return true; }
    if (enc == Mat::ACID) { w.setBlockTypeNotify(x, y, z, d == 0 ? bid::MAGMA : bid::NETHERRACK); fizz(w); return true; }
    return false;
  }
  // acid (nether branch: no fizz)
  if (enc == Mat::LAVA || enc == Mat::WATER) {
    w.setBlockTypeNotify(x, y, z, d == 0 ? bid::BRIMTHAW : bid::BRIMSTONE);
    return true;
  }
  return false;
}

void checkForHarden(DecoWorld& w, int x, int y, int z, uint16_t self) {
  if (w.get(x, y, z) != self) return;
  FluidKind k = fluidKind(self);
  static const int dirs[5] = {D_NORTH, D_SOUTH, D_WEST, D_EAST, D_UP};
  for (int dd : dirs) {
    Mat m = w.mat(x + DX[dd], y + DY[dd], z + DZ[dd]);
    if (fluidHarden(w, k, x, y, z, m)) return;
  }
}

inline int getDepth(const DecoWorld& w, Mat m, int x, int y, int z) {
  if (w.mat(x, y, z) != m) return -1;
  return w.data(x, y, z) & 0xF;
}
inline bool fluidBlockingId(uint16_t b) {
  return !isFluid(b) && !hasF(b, T_BROKEN_BY_FLUIDS) && !hasF(b, T_PLACE_OVERWRITES);
}
inline bool fluidCanSpreadToMat(FluidKind k, Mat m) {
  if (k == FK_LAVA) return m != Mat::WATER && m != Mat::ACID;
  if (k == FK_WATER) return m != Mat::LAVA && m != Mat::ACID;
  return m != Mat::LAVA && m != Mat::WATER;
}
bool canSpreadTo(const DecoWorld& w, FluidKind k, int x, int y, int z) {
  Mat m = w.mat(x, y, z);
  if (m == fluidMat(k)) return false;
  if (!fluidCanSpreadToMat(k, m)) return false;
  return !fluidBlockingId(w.get(x, y, z));
}
inline int pos2index(int x, int z) { return (x & 0xF) | ((z & 0xF) << 4); }
inline void index2pos(int i, int& x, int& z) {
  x = i & 0xF; z = (i >> 4) & 0xF;
  if (x >= 8) x |= (int)0xFFFFFFF8;
  if (z >= 8) z |= (int)0xFFFFFFF8;
}

int getSpreadDirections(const DecoWorld& w, FluidKind k, int ox, int oy, int oz, int spreadDistance) {
  std::vector<int> holeBuf, obstacleBuf;
  holeBuf.reserve(64); obstacleBuf.reserve(64);
  int dist[256];
  for (int& v : dist) v = 0x7FFFFFFE;
  const int MAXV = 0x7FFFFFFF;
  int neighMask = 0;
  Mat self = fluidMat(k);
  int layer;
  for (layer = 1; layer <= spreadDistance; ++layer) {
    int obstaclesBefore = (int)obstacleBuf.size();
    for (int x = -layer; x <= layer; ++x) {
      int rz = layer - std::abs(x);
      int z = -rz;
      while (true) {
        int qx = ox + x, qy = oy, qz = oz + z;
        int i = pos2index(x, z);
        uint16_t b = w.get(qx, qy, qz);
        bool obstructed;
        if (b == 0) obstructed = false;
        else if (fluidBlockingId(b)) obstructed = true;
        else obstructed = matOf(b) == self && w.data(qx, qy, qz) == 0;
        if (obstructed) {
          obstacleBuf.push_back(i);
          dist[i] = MAXV;
          if (layer == 1) {
            if (x != 0) neighMask = (int8_t)(neighMask | (x == -1 ? 1 : 2));
            else neighMask = (int8_t)(neighMask | (z == -1 ? 4 : 8));
          }
        } else {
          uint16_t b2 = w.get(qx, qy - 1, qz);
          bool hole = b2 == 0 ? true : !fluidBlockingId(b2);
          if (hole) { holeBuf.push_back(i); dist[i] = 0; }
        }
        if (z == rz) break;
        z = rz;
      }
    }
    int blocksThisLayer = 4 * layer;
    int holes = (int)holeBuf.size();
    if (obstaclesBefore == 0 && holes != 0) {
      if (holes == blocksThisLayer) return 15;
      int mask = 0;
      for (int hi : holeBuf) {
        int x, z; index2pos(hi, x, z);
        if (x != 0) mask = (int8_t)(mask | (x < 0 ? 1 : 2));
        if (z != 0) mask = (int8_t)(mask | (z < 0 ? 4 : 8));
      }
      return mask;
    }
    if ((int)obstacleBuf.size() - obstaclesBefore == blocksThisLayer) break;
  }
  if (holeBuf.empty()) return (int8_t)(~neighMask);
  for (size_t q = 0; q < holeBuf.size(); ++q) {
    int i = holeBuf[q];
    int d = dist[i];
    if (d == MAXV) continue;
    int x, z; index2pos(i, x, z);
    for (int s = 0; s < 4; ++s) {
      int x2 = x, z2 = z;
      if (s == 0) --x2; else if (s == 1) ++x2; else if (s == 2) --z2; else ++z2;
      int od = std::abs(x2) + std::abs(z2);
      if (od >= layer || od == 0) continue;
      int i2 = pos2index(x2, z2);
      int d2 = dist[i2];
      if (d2 == MAXV || d2 <= d + 1) continue;
      dist[i2] = d + 1;
      holeBuf.push_back(i2);
    }
  }
  int dW = dist[pos2index(-1, 0)], dE = dist[pos2index(1, 0)], dN = dist[pos2index(0, -1)], dS = dist[pos2index(0, 1)];
  int dm = std::min(std::min(dW, dE), std::min(dN, dS));
  int mask = 0;
  if (dW == dm) mask |= 1;
  if (dE == dm) mask |= 2;
  if (dN == dm) mask |= 4;
  if (dS == dm) mask |= 8;
  return mask;
}

void setFluidStill(DecoWorld& w, FluidKind k, int x, int y, int z) {
  w.setBlockTypeData(x, y, z, stillOf(k), w.data(x, y, z));
}

void flowIntoBlock(DecoWorld& w, FluidKind k, int x, int y, int z, int meta) {
  if (!canSpreadTo(w, k, x, y, z)) return;
  if (k == FK_LAVA) fizz(w);  // FluidLava.onFlowIntoBlock; water/acid drop the replaced block's items
  w.setBlockTypeDataNotify(x, y, z, flowingOf(k), meta);
}

void fluidFlowingTick(DecoWorld& w, int x, int y, int z, uint16_t self) {
  FluidKind k = fluidKind(self);
  Mat m = fluidMat(k);
  int local = getDepth(w, m, x, y, z);
  const int mod = 1;  // getFlowDecayMod in the Nether: water 1, lava 1, acid 1
  if (local > 0) {
    int sur = -100;
    auto highest = [&](int qx, int qy, int qz, int cur) {
      int fd = getDepth(w, m, qx, qy, qz);
      if (fd < 0) return cur;
      if (fd >= 8) fd = 0;
      return cur >= 0 && fd >= cur ? cur : fd;
    };
    sur = highest(x - 1, y, z, sur);
    sur = highest(x + 1, y, z, sur);
    sur = highest(x, y, z - 1, sur);
    sur = highest(x, y, z + 1, sur);
    int nfd = sur + mod;
    if (nfd >= 8 || sur < 0) nfd = -1;
    int above = getDepth(w, m, x, y + 1, z);
    if (above >= 8) nfd = above;
    else if (above >= 0) nfd = 8;
    // canBecomeSource is false for every fluid in the Nether
    if (nfd != local) {
      local = nfd;
      if (local < 0) w.setBlockTypeNotify(x, y, z, 0);
      else {
        w.setBlockDataNotify(x, y, z, local);
        w.scheduleUpdate(x, y, z, self);
      }
    } else setFluidStill(w, k, x, y, z);
  } else setFluidStill(w, k, x, y, z);
  if (canSpreadTo(w, k, x, y - 1, z)) {
    w.setBlockTypeDataNotify(x, y - 1, z, self, 8);
  } else if (local >= 0 && (local == 0 || fluidBlockingId(w.get(x, y - 1, z)))) {
    int meta = local < 8 ? local + mod : 1;
    int spread = (8 - meta) / mod;
    if (spread <= 0) return;
    int f = getSpreadDirections(w, k, x, y, z, spread);
    if (f & 1) flowIntoBlock(w, k, x - 1, y, z, meta);
    if (f & 2) flowIntoBlock(w, k, x + 1, y, z, meta);
    if (f & 4) flowIntoBlock(w, k, x, y, z - 1, meta);
    if (f & 8) flowIntoBlock(w, k, x, y, z + 1, meta);
  }
}

// ---- fire / magma / misc -------------------------------------------------------------------------
inline bool fireValid(const DecoWorld& w, int x, int y, int z) {
  return w.normalCube(x, y - 1, z);  // || canNeighborCatchFire: nothing in the Nether is flammable
}
void fireTick(DecoWorld& w, int x, int y, int z, uint16_t self, JavaRandom& rand) {
  uint16_t below = w.get(x, y - 1, z);
  bool infini = hasF(below, self == bid::FIRE ? T_INF_BURN : T_INF_BURN_SULF);
  if (!fireValid(w, x, y, z)) w.setBlockTypeNotify(x, y, z, 0);
  int meta = w.data(x, y, z);
  if (meta < 15) w.setBlockData(x, y, z, meta + rand.nextInt(3) / 2);
  w.scheduleUpdate(x, y, z, self);
  if (!infini) {
    if (!w.normalCube(x, y - 1, z) || meta > 3) w.setBlockTypeNotify(x, y, z, 0);
    return;
  }
  // checkBurn x6 (burnChance is 0 for every Nether block): only consumes rand
  rand.nextInt(300); rand.nextInt(300); rand.nextInt(250); rand.nextInt(250); rand.nextInt(300); rand.nextInt(300);
  // spreading needs flammable neighbours: none
}
bool magmaCanMelt(const DecoWorld& w, int x, int y, int z) {
  bool melt = false;
  for (int d = 0; d < 6; ++d) {
    Mat m = w.mat(x + DX[d], y + DY[d], z + DZ[d]);
    if (m == Mat::WATER || m == Mat::ACID) return false;
    melt |= m == Mat::LAVA;
  }
  return melt;
}
inline int dirOf(int data) { int i = data & 7; return i < 6 ? i : 0; }  // Direction.fromId (NONE.id == 0)
bool growthCanStay(const DecoWorld& w, int x, int y, int z) {
  int data = w.data(x, y, z);
  int shape = data & 0x18, d = dirOf(data);
  int bx = x - DX[d], by = y - DY[d], bz = z - DZ[d];
  if (shape == 16) return w.get(bx, by, bz) == bid::RUBYGLASS_GROWTH && (w.data(bx, by, bz) & 0x18) == 8;
  return w.normalCube(bx, by, bz);
}
}  // namespace

bool layerAshCanPlaceAt(const DecoWorld& w, int x, int y, int z) {
  uint16_t b = w.get(x, y - 1, z);
  bool supported = solidRender(b) || b == bid::ICE || b == bid::RUBYGLASS_CRYSTAL || b == bid::RUBYGLASS_GROWTH;
  return supported && hasF(w.get(x, y, z), T_PLACE_OVERWRITES);
}

void blockOnPlaced(DecoWorld& w, int x, int y, int z, uint16_t id) {
  switch (id) {
    case bid::WATER_STILL: case bid::LAVA_STILL: case bid::ACID_STILL:
      checkForHarden(w, x, y, z, id);
      return;
    case bid::WATER_FLOWING: case bid::LAVA_FLOWING: case bid::ACID_FLOWING:
      checkForHarden(w, x, y, z, id);
      if (w.get(x, y, z) == id) w.scheduleUpdate(x, y, z, id);
      return;
    case bid::FIRE: case bid::FIRE_SULFURIC:
      // (obsidian below + portal frame -> nether portal: impossible in generated terrain, not modelled)
      if (!w.normalCube(x, y - 1, z)) w.setBlockTypeNotify(x, y, z, 0);
      else w.scheduleUpdate(x, y, z, id);
      return;
    case bid::MAGMA: case bid::BRIMTHAW: case bid::SULFUR:
      w.wrand.nextInt(5); w.wrand.nextInt(5);
      w.scheduleUpdate(x, y, z, id);
      return;
    case bid::THERMAL_VENT: {
      bool ventBelow = w.get(x, y - 1, z) == bid::THERMAL_VENT;
      w.setBlockData(x, y, z, ventBelow ? w.wrand.nextInt(4) : 0);
      return;
    }
    default: return;
  }
}

void blockOnNeighborChanged(DecoWorld& w, int x, int y, int z, uint16_t id, uint16_t src) {
  (void)src;
  switch (id) {
    case bid::WATER_STILL: case bid::LAVA_STILL: case bid::ACID_STILL: {
      checkForHarden(w, x, y, z, id);
      if (w.get(x, y, z) == id) {  // setFlowing
        FluidKind k = fluidKind(id);
        int meta = w.data(x, y, z);
        w.setBlockTypeData(x, y, z, flowingOf(k), meta);
        w.scheduleUpdate(x, y, z, flowingOf(k));
      }
      return;
    }
    case bid::WATER_FLOWING: case bid::LAVA_FLOWING: case bid::ACID_FLOWING:
      checkForHarden(w, x, y, z, id);
      if (w.get(x, y, z) == id) w.scheduleUpdate(x, y, z, id);
      return;
    case bid::FIRE: case bid::FIRE_SULFURIC:
      if (!w.normalCube(x, y - 1, z)) w.setBlockTypeNotify(x, y, z, 0);
      return;
    case bid::MAGMA: case bid::BRIMTHAW: case bid::SULFUR:
      w.wrand.nextInt(5); w.wrand.nextInt(5);
      w.scheduleUpdate(x, y, z, id);
      return;
    case bid::THERMAL_VENT:
      if (w.get(x, y - 1, z) != bid::THERMAL_VENT && w.data(x, y, z) != 0) w.setBlockData(x, y, z, 0);
      return;
    case bid::RUBYGLASS_GROWTH: {
      int data = w.data(x, y, z);
      int shape = data & 0x18, d = dirOf(data);
      if (!growthCanStay(w, x, y, z)) { w.setBlockTypeNotify(x, y, z, 0); return; }
      if (shape == 8 && (w.data(x + DX[d], y + DY[d], z + DZ[d]) & 0x18) != 16)
        w.setBlockTypeDataNotify(x, y, z, bid::RUBYGLASS_GROWTH, 0 | d);
      return;
    }
    case bid::BONE_PILE: case bid::BOULDER_MAGMATIC: case bid::BOULDER_SULFURIC: case bid::SOUL_CATCHER:
      if (!solidRender(w.get(x, y - 1, z))) w.setBlockTypeNotify(x, y, z, 0);
      return;
    case bid::LAYER_ASH:
      if (!layerAshCanPlaceAt(w, x, y, z)) w.setBlockTypeNotify(x, y, z, 0);
      return;
    default: return;
  }
}

void blockUpdateTick(DecoWorld& w, int x, int y, int z, uint16_t id, JavaRandom& rand) {
  switch (id) {
    case bid::WATER_FLOWING: case bid::LAVA_FLOWING: case bid::ACID_FLOWING:
      fluidFlowingTick(w, x, y, z, id);
      return;
    case bid::FIRE: case bid::FIRE_SULFURIC:
      fireTick(w, x, y, z, id, rand);
      return;
    case bid::MAGMA:
      if (magmaCanMelt(w, x, y, z)) w.setBlockTypeNotify(x, y, z, bid::LAVA_FLOWING);
      return;
    case bid::BRIMTHAW: {  // BlockLogicBrimthaw.canMelt: next to acid, not next to water/lava
      bool melt = false;
      for (int d = 0; d < 6; ++d) {
        Mat m = w.mat(x + DX[d], y + DY[d], z + DZ[d]);
        if (m == Mat::WATER || m == Mat::LAVA) return;
        melt |= m == Mat::ACID;
      }
      if (melt) w.setBlockTypeNotify(x, y, z, bid::ACID_FLOWING);
      return;
    }
    case bid::LAVA_STILL: {  // FluidLava.updateTickStill: only consumes rand (no flammables in the Nether)
      int cnt = rand.nextInt(3);
      int px = x, py = y, pz = z;
      for (int i = 0; i < cnt; ++i) {
        px += rand.nextInt(3) - 1; ++py; pz += rand.nextInt(3) - 1;
        if (!w.areBlocksLoaded(px, pz, 2)) continue;
        uint16_t b = w.get(px, py, pz);
        if (b == 0) continue;
        if (!hasF(b, P_BLOCKS_MOTION)) continue;
        return;
      }
      return;
    }
    default: return;
  }
}

}  // namespace bta
