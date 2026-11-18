// sim/catchup.cpp — time keeps passing where nobody is (sim/catchup.h).
#include "sim/catchup.h"
#include "sim/worldsys.h"
#include "sim/world_life.h"
#include "sim/cave_drips.h"
#include "sim/dyntree.h"
#include "sim/gameplay.h"
#include "sim/blockinfo.h"
#include "sim/crafting.h"
#include "sim/brewing.h"
#include "sim/redstone.h"
#include "sim/creature.h"
#include "sim/weather.h"
#include "sim/rules.h"
#include "common/noise.h"
#include <deque>
#include <random>

namespace dq {

namespace {
const double RT = 68.27;   // the random tick's odds are written per this many ticks (sim/worldsys.cpp)
const double OWED_MIN = 2.5;   // seconds missed before a chunk is worth catching up (the live ticks cover less)
const double BUDGET_MS = 1.0;  // catch-up work per tick
const int CELL_CAP = 24;       // growth events one block makes up in one catch-up (a crop's stages, a cane's height...)
const double HOPPER_EVERY = 0.4;   // seconds between a hopper's moves (8 game ticks)

double rnd01() { static thread_local std::mt19937 g{std::random_device{}()}; return std::uniform_real_distribution<double>(0, 1)(g); }

// the silent effects: a catch-up far away makes no sounds or sparkles
LevelFx& quietFx() { static LevelFx f; return f; }
struct QuietFx { Level& lv; LevelFx* was; explicit QuietFx(Level& l) : lv(l), was(l.fx) { l.fx = &quietFx(); } ~QuietFx() { lv.fx = was; } };

struct Job {
  int realm = 0; ChunkKey key{0, 0, 0};
  double blocks = 0, machines = 0;   // seconds owed: random ticks and hoppers, furnaces and stands
  int phase = 0;                     // 0 not started, 1 growing blocks, 2 machines and hoppers, 3 done
  std::vector<int> cells; size_t ci = 0;
  std::vector<ivec3> hoppers, furnaces, brews;
  double T = 0, W = 0; int win = 0, nwin = 0;
  long roundsLeft = -1;              // hopper rounds left in this window (-1: the window hasn't started)
  bool machinesDone = false;         // this window's furnaces and stands
};
struct State {
  std::deque<Job> q;
  double scanT = 0;
  int scanSlice = 0;
  CatchUpStats stats;
};
State& st(Level& lv) { return levelSlot<State>(lv, "catchUp"); }

double num(const json& j, const char* k, double d) { auto it = j.find(k); return it != j.end() && it->is_number() ? it->get<double>() : d; }

// ---- growing blocks: the odds of one random tick, and what one success does -------------------------------------------
double cellChance(Level& lv, World& w, int x, int y, int z, BlockId id, u8 meta) {
  const Registry& R = reg(); const KnownBlocks& B = KB();
  const BlockDef& d = R.block(id);
  if (!id) return 0;
  if (d.key == "pointed_dripstone") {
    if (!(meta & 1)) return 0;
    const CaveDrip c = caveDripAt(w, x, y, z);
    if (!c.liquid) return 0;
    return 1.0 / ((c.liquid == 1 ? 150.0 : 420.0) / (0.75 + 0.25 * std::min(c.length, 5)));
  }
  if (B.farmland && id == B.farmland) return (meta & 7) == 7 && waterNear(w, x, y, z, 4) ? 0 : 1.0 / 20;   // (moist by water: stays so)
  if (bhas(id, BF::Tree)) return 1 / (RT * 7);
  if (id == B.berry_bush_empty || d.has("refill")) return 1 / (RT * 5);
  if (bhas(id, BF::Crop)) {
    const int m = meta & 15, stages = d.props["crop"].value("stages", 1);
    if (m >= stages - 1) return d.props["crop"].contains("fruit") ? 1.0 / 340 : 0;   // a ripe stem sets fruit
    if (!d.has("soil") && !d.has("cocoa")) { const u8 l = w.getLight(x, y, z); if (std::max(l >> 4, l & 15) < 9) return 0; }   // (in the dark it never grows)
    const bool wet = d.has("soil") || d.has("cocoa") || farmlandMoist(w, x, y - 1, z) || waterNear(w, x, y - 1, z, 4);
    double p = wet ? 1 / (RT * 4) : 1 / (RT * 8);
    if (seasonOf(lv) == 3 && skyLandOf(dimOfY(y)) == DIM_OVERWORLD && w.realm == REALM_MAIN) p *= 0.3;   // winter
    return p;
  }
  if (id == B.sugar_cane || id == B.cactus || id == B.kelp) {
    const BlockId above = w.getBlock(x, y + 1, z);
    if (id == B.kelp ? R.tLiquid[above] != 1 : above != AIR) return 0;
    return id == B.kelp ? 0.14 / RT : 1 / (RT * 16);
  }
  if (id == B.bamboo) return w.getBlock(x, y + 1, z) == AIR ? 1.0 / 205 : 0;
  if (id == B.twisting_vines) return w.getBlock(x, y + 1, z) == AIR ? 1.0 / 680 : 0;
  if (id == B.weeping_vines || id == B.cave_vines) return w.getBlock(x, y - 1, z) == AIR ? 1.0 / 680 : 0;
  if (d.has("rootySoil")) return dynTreesOn(lv) ? 1 / (RT * 0.5) : 0;
  if (d.has("netherReed") || d.has("netherCactus")) return w.getBlock(x, y + 1, z) == AIR ? 1 / (RT * 16) * (d.has("netherReed") ? 0.75 : 1) : 0;
  if (d.has("hive")) return (meta & 0x0c) != 0x0c ? 1.0 / 140 : 0;
  if (id == B.budding_amethyst) return 1.0 / (340 * 6);
  if (d.has("oxidizes")) return 1.0 / 2400;
  static const BlockId brownM = R.blockId("brown_mushroom"), redM = R.blockId("red_mushroom"), vine = R.blockId("vine"),
    mycelium = R.blockId("mycelium"), podzol = R.blockId("podzol");
  if (id && (id == brownM || id == redM)) {
    const BlockId below = w.getBlock(x, y - 1, z);
    const u8 l = w.getLight(x, y, z);
    const bool ground = below && (below == mycelium || below == podzol);
    return !ground && std::max(l >> 4, l & 15) > 12 ? 0.5 : 1.0 / 25;
  }
  if (id && id == vine) return 0.25;
  return 0;
}
// one success; `at` follows the growing tip (a cane's new top, a vine's new end). false = nothing more can come of it
bool cellStep(Level& lv, World& w, ivec3& at, BlockId id, u8 meta) {
  const Registry& R = reg(); const KnownBlocks& B = KB();
  const BlockDef& d = R.block(id);
  const int x = at.x, y = at.y, z = at.z;
  if (d.key == "pointed_dripstone") {
    static const BlockId cauldron = R.blockId("cauldron");
    int k = 1; while (k <= 11 && w.getBlock(x, y - k, z) == AIR) k++;
    if (k > 11 || w.getBlock(x, y - k, z) != cauldron || cauldronLevel(w.getMeta(x, y - k, z)) >= 3) return false;
    caveDripTick(w, x, y, z, 0.0);
    return true;
  }
  if (B.farmland && id == B.farmland) { farmlandTick(lv, w, x, y, z, meta); return true; }
  if (bhas(id, BF::Tree)) { saplingStep(lv, w, x, y, z, id, meta); return w.getBlock(x, y, z) == id; }
  if (id == B.berry_bush_empty) { w.setBlock(x, y, z, B.berry_bush); return false; }
  if (d.has("refill")) { if (BlockId f = R.blockId(d.str("refill"))) w.setBlock(x, y, z, f); return false; }
  if (bhas(id, BF::Crop)) {
    const int m = meta & 15, stages = d.props["crop"].value("stages", 1);
    if (m >= stages - 1) return stemFruit(w, x, y, z, d);
    w.setBlock(x, y, z, id, (u8)((meta & ~15) | (m + 1)));
    return true;
  }
  if (id == B.sugar_cane || id == B.cactus || id == B.kelp) {
    int h = 1; while (h < 4 && w.getBlock(x, y - h, z) == id) h++;
    if (h >= (id == B.kelp ? 8 : 3)) return false;
    w.setBlock(x, y + 1, z, id, id == B.kelp ? WLOG : 0);
    at.y++;
    return true;
  }
  if (id == B.bamboo) {
    int h = 1; while (h < 16 && w.getBlock(x, y - h, z) == id) h++;
    if (h >= 12 + (int)(hash32(x, 0, z, 3) % 5)) return false;
    w.setBlock(x, y + 1, z, id, 0); at.y++;
    return true;
  }
  if (id == B.twisting_vines) { int h = 1; while (h < 26 && w.getBlock(x, y - h, z) == id) h++; if (h >= 25) return false; w.setBlock(x, y + 1, z, id, 0); at.y++; return true; }
  if (id == B.weeping_vines || id == B.cave_vines) {
    int h = 1; while (h < 26 && bhas(w.getBlock(x, y + h, z), BF::Hangs)) h++;
    if (h >= 25) return false;
    w.setBlock(x, y - 1, z, id == B.cave_vines && rnd01() < 0.15 ? B.cave_vines_lit : id, 0); at.y--;
    return w.getBlock(at.x, at.y, at.z) == id;
  }
  if (d.has("rootySoil")) return growDynTree(lv, w, x, y, z, (u32)(rnd01() * 4294967295.0));
  if (d.has("netherReed") || d.has("netherCactus")) {
    int h = 1; while (h < 4 && w.getBlock(x, y - h, z) == id) h++;
    if (h >= 3) return false;
    w.setBlock(x, y + 1, z, id, 0); at.y++;
    return true;
  }
  if (d.has("hive")) { w.setBlock(x, y, z, id, meta + 4); return true; }
  if (id == B.budding_amethyst) {
    const BlockId nid = w.getBlock(x, y + 1, z);
    const BlockId next = nid == AIR ? B.small_amethyst_bud : nid == B.small_amethyst_bud ? B.medium_amethyst_bud : nid == B.medium_amethyst_bud ? B.large_amethyst_bud : nid == B.large_amethyst_bud ? B.amethyst_cluster : (BlockId)0;
    if (!next) return false;
    w.setBlock(x, y + 1, z, next, 0);
    return true;
  }
  if (d.has("oxidizes")) { if (BlockId o = R.blockId(d.str("oxidizes"))) w.setBlock(x, y, z, o, meta); return true; }
  static const BlockId brownM = R.blockId("brown_mushroom"), redM = R.blockId("red_mushroom"), vine = R.blockId("vine");
  if (id == brownM || id == redM) { mushroomTick(lv, w, x, y, z, id, true); return w.getBlock(x, y, z) == id; }
  if (id == vine) { vineTick(lv, w, x, y, z, meta, true); return true; }
  return false;
}
// make up `calls` random ticks of the block at (x,y,z): the ticks until its next success are drawn from its odds now
int catchUpCell(Level& lv, World& w, int x, int y, int z, double calls) {
  ivec3 at(x, y, z);
  int n = 0;
  for (; n < CELL_CAP; n++) {
    const BlockId id = w.getBlock(at.x, at.y, at.z);
    const u8 meta = w.getMeta(at.x, at.y, at.z);
    const double p = cellChance(lv, w, at.x, at.y, at.z, id, meta);
    if (p <= 0) break;
    const double k = p >= 1 ? 1 : std::floor(std::log(1 - rnd01()) / std::log1p(-p)) + 1;   // ticks to the next success
    if (k > calls) break;
    calls -= k;
    if (!cellStep(lv, w, at, id, meta)) { n++; break; }
  }
  return n;
}

// ---- machines ---------------------------------------------------------------------------------------------------------
bool furnaceIdle(const json& e) {   // no fire, and none to be lit (tickFurnace lights it the moment it can)
  return e.value("burn", 0.0) <= 0;
}
// a furnace's `secs`, a step at a time (half an item's time: the cook lands on the item exactly); false = it went idle
bool runFurnace(json& e, double secs, const std::string& kind, long& steps) {
  double left = secs;
  while (left > 1e-6) {
    const double dt = std::min(left, (double)SMELT_TIME / 2);
    const bool changed = tickFurnace(e, dt, kind);
    steps++;
    left -= dt;
    if (furnaceIdle(e) && !changed) return false;
  }
  return true;
}
bool runBrew(Level& lv, World& w, json& e, double secs, ivec3 p, long& steps) {
  double left = secs;
  const double each = std::max(1.0, (double)magicData().brewTime / 2);
  while (left > 1e-6) {
    const double dt = std::min(left, each);
    const bool changed = tickBrew(lv, w, e, dt, p.x, p.y, p.z);
    steps++;
    left -= dt;
    if (!changed && !(e.value("t", 0.0) > 0)) return false;
  }
  return true;
}

void prepare(Level& lv, World& w, Chunk& c, Job& j) {
  const Registry& R = reg();
  const rs::Tables& t = rs::T();
  for (int sy = 0; sy < SECS; sy++) {
    const Section* sec = c.section(sy);
    if (!sec) continue;
    for (int i = 0; i < 4096; i++) {
      const BlockId b = sec->blocks[i];
      if (!b) continue;
      const int ly = sy * 16 + (i >> 8), lz = (i >> 4) & 15, lx = i & 15;
      if (j.blocks > 0 && R.tTick[b]) j.cells.push_back(colIdx(lx, ly, lz));
      if (j.blocks > 0 && t.ticker[b] == 1) j.hoppers.push_back(ivec3(c.worldX() + lx, c.baseY() + ly, c.worldZ() + lz));
    }
  }
  if (j.machines > 0) for (auto& [i, e] : c.blockEntities) {
    if (!e.is_object()) continue;
    const int lx = i & 15, lz = (i >> 4) & 15, ly = i >> 8;
    const ivec3 p(c.worldX() + lx, c.baseY() + ly, c.worldZ() + lz);
    const std::string type = e.value("type", "");
    if (type == "furnace") { const BlockInfo& bi = binfo(c.get(lx, ly, lz)); if (!bi.furnace.empty() && bi.furnace != "trommel") j.furnaces.push_back(p); }
    else if (type == "brew" && R.block(c.get(lx, ly, lz)).key == "brewing_stand") j.brews.push_back(p);
  }
  // hoppers: the ones higher up first (what falls down a chain moves on in the same round), then along x and z
  std::sort(j.hoppers.begin(), j.hoppers.end(), [](const ivec3& a, const ivec3& b) { return a.y != b.y ? a.y > b.y : a.x != b.x ? a.x < b.x : a.z < b.z; });
  // windows: the hoppers and the machines they feed take turns, a slice of the time each
  j.T = std::max(j.hoppers.empty() ? 0.0 : j.blocks, (j.furnaces.empty() && j.brews.empty()) ? 0.0 : j.machines);
  j.nwin = j.T > 0 ? (int)std::ceil(j.T / std::max(30.0, j.T / 48)) : 0;
  j.W = j.nwin ? j.T / j.nwin : 0;
  j.phase = 1;
  (void)lv; (void)w;
}

// how much of [a, b) falls in the last `len` seconds of [0, T)
double overlap(double a, double b, double T, double len) { return std::max(0.0, std::min(b, T) - std::max(a, T - len)); }

// some of a job; true = it's finished
bool work(Level& lv, Job& j, double until) {
  State& S = st(lv);
  World& w = lv.world(j.realm);
  Chunk* c = w.chunk(j.key.dim, j.key.cx, j.key.cz);
  if (!c) return true;
  QuietFx quiet(lv);
  if (j.phase == 0) { prepare(lv, w, *c, j); if (nowSeconds() > until) return false; }
  const double tickSpeed = std::clamp(ruleNum(lv, "randomTickSpeed"), 0.0, 100.0);
  if (j.phase == 1) {
    const double calls = j.blocks * tickSpeed;
    for (; j.ci < j.cells.size(); j.ci++) {
      if ((j.ci & 7) == 7 && nowSeconds() > until) return false;
      const int i = j.cells[j.ci];
      S.stats.events += catchUpCell(lv, w, c->worldX() + (i & 15), c->baseY() + (i >> 8), c->worldZ() + ((i >> 4) & 15), calls);
    }
    j.phase = 2;
  }
  if (j.phase == 2) {
    Redstone* rs = redstoneOf(lv, j.realm);
    recipeGate().immersive = w.immersive;
    while (j.win < j.nwin) {
      const double a = j.win * j.W, b = a + j.W;
      // the hoppers' share of this window, round by round (a round: every hopper once, the highest first)
      if (j.roundsLeft < 0) j.roundsLeft = rs ? (long)std::floor(overlap(a, b, j.T, j.blocks) / HOPPER_EVERY) : 0;
      while (j.roundsLeft > 0 && !j.hoppers.empty()) {
        if (nowSeconds() > until) return false;
        bool moved = false;
        for (const ivec3& h : j.hoppers) if (rs->hopperOnce(h.x, h.y, h.z)) { moved = true; S.stats.hopperMoves++; }
        j.roundsLeft--;
        if (!moved) j.roundsLeft = 0;   // (nothing to move: the rest of the window is the same)
      }
      if (!j.machinesDone) {
        const double ms = overlap(a, b, j.T, j.machines);
        if (ms > 0) {
          for (const ivec3& p : j.furnaces) if (json* e = w.blockEntity(p.x, p.y, p.z)) {
            const std::string kind = binfo(w.getBlock(p.x, p.y, p.z)).furnace;
            if (kind.empty()) continue;
            runFurnace(*e, ms, kind, S.stats.machineSteps);
            w.blockEntityChanged(p.x, p.y, p.z);
          }
          for (const ivec3& p : j.brews) if (json* e = w.blockEntity(p.x, p.y, p.z)) { runBrew(lv, w, *e, ms, p, S.stats.machineSteps); w.blockEntityChanged(p.x, p.y, p.z); }
        }
        j.machinesDone = true;
      }
      j.win++; j.roundsLeft = -1; j.machinesDone = false;
      if (nowSeconds() > until) return j.win >= j.nwin;
    }
    j.phase = 3;
  }
  return true;
}

void runQueue(Level& lv, double budgetMs) {
  State& S = st(lv);
  if (S.q.empty()) return;
  const double t0 = nowSeconds(), until = budgetMs > 0 ? t0 + budgetMs / 1000 : 1e300;
  while (!S.q.empty()) {
    Job& j = S.q.front();
    if (work(lv, j, until)) { S.q.pop_front(); S.stats.chunks++; }
    if (nowSeconds() > until) break;
  }
  const double ms = (nowSeconds() - t0) * 1000;
  S.stats.totalMs += ms;
  if (budgetMs > 0) S.stats.maxTickMs = std::max(S.stats.maxTickMs, ms);
}

void addJob(State& S, int realm, ChunkKey key, double blocks, double machines) {
  for (Job& j : S.q) if (j.realm == realm && j.key == key && j.phase == 0) { j.blocks += blocks; j.machines += machines; return; }
  Job j; j.realm = realm; j.key = key; j.blocks = blocks; j.machines = machines;
  S.q.push_back(std::move(j));
}

// a chunk going away with work still owed: what was queued is done now (rare; usually it's long done)
class CatchUpWorld : public WorldSystem {
public:
  explicit CatchUpWorld(Level* lv) : lv(lv) {}
  Level* lv;
  const char* name() const override { return "catchUp"; }
  void onChunkUnloading(World& w, Chunk& c) override {
    State& S = st(*lv);
    for (auto it = S.q.begin(); it != S.q.end();) {
      if (it->realm == w.realm && it->key == c.key()) { while (!work(*lv, *it, 1e300)) {} it = S.q.erase(it); }
      else ++it;
    }
  }
};

// slice / slices: only that share of the chunks (each chunk is in one): a second's look is spread over its ticks
void stampScan(Level& lv, int slice = 0, int slices = 1) {
  State& S = st(lv);
  const double now = simClock(lv);
  const int R = simDistance(lv);
  const bool on = rule(lv, "catchUp");
  for (auto& [realm, wp] : lv.allRealms()) {
    World& w = *wp;
    if (!w.system<CatchUpWorld>()) w.addSystem(std::make_shared<CatchUpWorld>(&lv));
    std::vector<ivec2> centers;
    for (auto& e : lv.entities()) if (e && !e->removed && e->isPlayer() && e->realm == realm) centers.push_back({ifloor(e->pos.x) >> 4, ifloor(e->pos.z) >> 4});
    for (auto& [k, c] : w.chunks()) {
      if (slices > 1 && ((k.cx * 7 + k.cz * 13) % slices + slices) % slices != slice) continue;
      if (!c->extra.is_object()) c->extra = json::object();
      json& x = c->extra;
      bool near = false;
      for (auto& p : centers) if (std::abs(k.cx - p.x) <= R && std::abs(k.cz - p.y) <= R) { near = true; break; }
      const double m = num(x, "simM", -1), t = num(x, "simT", -1);
      const double owedM = m >= 0 && m < now ? now - m : 0, owedT = near && t >= 0 && t < now ? now - t : 0;
      if (on && (owedM > OWED_MIN || owedT > OWED_MIN)) addJob(S, realm, k, owedT > OWED_MIN ? owedT : 0, owedM > OWED_MIN ? owedM : 0);
      x["simM"] = now;
      if (near || t < 0 || t > now) x["simT"] = now;
    }
  }
}
} // namespace

double simClock(const Level& lv) { return num(lv.info.data, "simClock", 0); }
int simDistance(Level& lv) { return std::clamp((int)ruleNum(lv, "simulationDistance"), 2, 32); }
CatchUpStats& catchUpStats(Level& lv) { return st(lv).stats; }
int catchUpPending(Level& lv) { return (int)st(lv).q.size(); }

double playSecondsBetween(Level& lv, double from, double to) {
  // the day (the first half of DAY_LEN) lasts dayMinutes, the night nightMinutes
  const double half = DAY_LEN / 2, dayS = std::max(30.0, ruleNum(lv, "dayMinutes") * 60), nightS = std::max(30.0, ruleNum(lv, "nightMinutes") * 60);
  double secs = 0;
  while (to < from) to += DAY_LEN;
  for (double t = from; t < to - 1e-9;) {
    const double ph = std::fmod(t, DAY_LEN), base = t - ph;
    const double end = std::min(to, base + (ph < half ? half : DAY_LEN));
    secs += (end - t) * (ph < half ? dayS : nightS) / half;
    t = end;
  }
  return secs;
}

void advanceSimClock(Level& lv, double secs) {
  if (secs <= 0 || lv.isRemote()) return;
  lv.info.data["simClock"] = simClock(lv) + secs;
  // the young grow up with the time
  for (auto& e : lv.entities()) if (auto* c = dynamic_cast<Creature*>(e.get())) if (!c->removed && c->baby && c->growT < 1e8f) c->growT = std::max(0.01f, c->growT - (float)secs);
  // (each chunk's stamp is looked at within the second, and what it owes is made up then)
}

void catchUpTick(Level& lv, double dt) {
  if (lv.isRemote()) return;
  lv.info.data["simClock"] = simClock(lv) + dt;
  State& S = st(lv);
  // the stamps: a share of the chunks each tick, every chunk once a second (all at once held a tick for a few
  // milliseconds with a wide view's thousand-odd chunks loaded)
  const int slices = 30;
  S.scanT += dt;
  const int upTo = std::min(slices, (int)(S.scanT * slices));
  const double t0 = nowSeconds();
  for (; S.scanSlice < upTo; S.scanSlice++) stampScan(lv, S.scanSlice, slices);
  S.stats.maxScanMs = std::max(S.stats.maxScanMs, (nowSeconds() - t0) * 1000);
  if (S.scanT >= 1) { S.scanT -= 1; S.scanSlice = 0; }
  runQueue(lv, BUDGET_MS);
}

void catchUpFlush(Level& lv) {
  stampScan(lv);
  runQueue(lv, 0);
}

} // namespace dq
