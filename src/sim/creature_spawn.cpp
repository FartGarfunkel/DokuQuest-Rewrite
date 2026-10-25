// sim/creature_spawn.cpp — the creature LevelSystem: where they turn up (game/bestiary.js spawnMobs with the
// monsters.js night crowd, spawnDepths, skymobs.js cockatrices), herds that come with the land and bee
// colonies (game/husbandry.js), bodies that don't share space (game/crowd.js: not ported by gameplay, so it
// lives here), item merging (game.js updateEntities), players' status effects; plus registerCreatures().
// No phantoms, ever.
#include "sim/creature.h"
#include "sim/creature_entities.h"
#include "sim/mob_combat.h"
#include "sim/physics.h"
#include "sim/player.h"
#include "core/registry.h"
#include "core/init.h"
#include "gen/structures_api.h"

namespace dq {

bool beeFlower(World& w, vec3 at, float R, int tries, ivec3& out);   // creature_beasts.cpp
vec3 hiveFront(World& w, ivec3 h);

static HabitatFn& habitatProvider() { static HabitatFn f; return f; }
void setHabitatProvider(HabitatFn f) { habitatProvider() = std::move(f); }

// ------------------------------------------------------------------ habitats (bestiary.js habitatAt)
// The original read the terrain's biome; the debug terrain has none, so this estimates it from the chunk's
// climate bytes (temp, humidity), the ground and the height — the terrain port can plug the real biome
// in with setHabitatProvider().
std::string habitatAt(World& w, int x, int z, BlockId ground) {
  if (habitatProvider()) { std::string h = habitatProvider()(w, x, z, ground); if (!h.empty()) return h; }
  const Registry& R = reg();
  int top = w.surfaceY(x, z, DIM_OVERWORLD);
  int h = top < 0 ? SEA_LEVEL : top - dimBase(DIM_OVERWORLD);
  float temp = 0, humid = 0;
  if (Chunk* c = w.chunk(DIM_OVERWORLD, x >> 4, z >> 4)) { int i = ((z & 15) * 16 + (x & 15)) * 2; temp = (c->climate[i] - 128) / 127.f; humid = (c->climate[i + 1] - 128) / 127.f; }
  const std::string& g = R.block(ground).key;
  BlockId topId = top >= 0 ? w.getBlock(x, top, z) : AIR;
  if (R.tLiquid[topId] == 1) { int d = 0; while (d < 12 && R.tLiquid[w.getBlock(x, top - d, z)] == 1) d++; if (d > 6) return "ocean"; }
  bool sand = g == "sand" || g == "red_sand" || g == "desert_sand" || g == "silver_sand";
  if (sand && h <= SEA_LEVEL + 3) return temp < -0.2f ? "snowy_beach" : "beach";
  if (sand && temp > 0.15f) return "desert";
  if (h > SEA_LEVEL + 40 || g == "snow_block") return temp < -0.3f || g == "snow_block" ? "snowy_peaks" : "mountains";
  if (temp < -0.3f) return temp < -0.55f ? "snowy_plains" : temp < -0.42f ? "snowy_taiga" : "taiga";
  if (h <= SEA_LEVEL + 1 && humid > 0.38f && temp > -0.1f) return "swamp";
  if (temp > 0.25f && humid > 0.3f) return "jungle";
  if (temp > 0.28f && humid < 0.02f) return "savanna";
  // trees about: forest
  int leaves = 0;
  for (int k = 0; k < 6; k++) { int ax = x + (int)(frand() * 13) - 6, az = z + (int)(frand() * 13) - 6, ay = w.surfaceY(ax, az, DIM_OVERWORLD); if (ay >= 0 && R.tLeaves[w.getBlock(ax, ay, az)]) leaves++; }
  if (leaves >= 2) return temp < 0 ? "birch_forest" : "forest";
  return "plains";
}

// [type, weight, smallest group, largest group]
struct SpawnE { const char* t; float w; int lo, hi; };
static const std::unordered_map<std::string, std::vector<SpawnE>>& spawnTable() {
  static const std::unordered_map<std::string, std::vector<SpawnE>> S = {
    {"plains", {{"horse", 8, 2, 4}, {"donkey", 2, 1, 2}, {"sheep", 12, 2, 4}, {"cow", 10, 2, 4}, {"pig", 10, 1, 3}, {"chicken", 10, 2, 4}, {"rabbit", 4, 1, 3}, {"turkey", 4, 2, 4}, {"mouse", 2, 1, 2}, {"duck", 3, 2, 3}, {"adder", 1, 1, 1}}},
    {"forest", {{"deer", 8, 2, 4}, {"boar", 5, 1, 3}, {"wolf", 5, 2, 4}, {"pig", 6, 1, 3}, {"chicken", 6, 1, 3}, {"sheep", 4, 2, 3}, {"cow", 4, 2, 3}, {"rabbit", 4, 1, 3}, {"raccoon", 3, 1, 2}, {"turkey", 3, 2, 3}, {"brown_bear", 2, 1, 2}, {"mouse", 2, 1, 2}, {"adder", 2, 1, 1}}},
    {"birch_forest", {{"deer", 8, 2, 4}, {"rabbit", 6, 1, 3}, {"pig", 5, 1, 3}, {"chicken", 5, 1, 3}, {"sheep", 5, 2, 3}, {"cow", 4, 2, 3}, {"fox", 3, 1, 2}}},
    {"taiga", {{"wolf", 8, 2, 4}, {"fox", 8, 1, 3}, {"rabbit", 6, 1, 3}, {"deer", 5, 2, 3}, {"sheep", 4, 2, 3}, {"pig", 3, 1, 2}, {"brown_bear", 3, 1, 2}}},
    {"snowy_taiga", {{"wolf", 8, 2, 4}, {"snow_fox", 8, 1, 3}, {"rabbit", 6, 1, 3}, {"deer", 3, 2, 3}, {"brown_bear", 2, 1, 2}}},
    {"snowy_plains", {{"polar_bear", 3, 1, 2}, {"snow_fox", 6, 1, 2}, {"rabbit", 8, 1, 3}, {"wolf", 3, 2, 4}}},
    {"snowy_peaks", {{"goat", 10, 2, 3}, {"snow_fox", 2, 1, 2}, {"rabbit", 3, 1, 2}, {"snow_leopard", 2, 1, 1}}},
    {"mountains", {{"goat", 10, 2, 3}, {"llama", 6, 2, 4}, {"sheep", 5, 2, 3}, {"rabbit", 3, 1, 2}}},
    {"savanna", {{"horse", 8, 2, 5}, {"donkey", 5, 1, 3}, {"llama", 4, 2, 4}, {"cow", 4, 2, 3}, {"armadillo", 7, 1, 2}, {"sheep", 3, 2, 3}, {"lioness", 3, 2, 3}, {"lion", 1, 1, 1}}},
    {"desert", {{"camel", 6, 1, 2}, {"rabbit", 8, 1, 3}, {"armadillo", 3, 1, 2}, {"rattlesnake", 4, 1, 1}, {"scorpion", 2, 1, 2}}},
    {"jungle", {{"parrot", 10, 1, 3}, {"ocelot", 5, 1, 2}, {"panda", 4, 1, 2}, {"chicken", 4, 1, 3}, {"pig", 3, 1, 2}, {"tiger", 2, 1, 1}, {"panther", 2, 1, 1}, {"green_snake", 4, 1, 2}}},
    {"swamp", {{"frog", 10, 2, 4}, {"duck", 6, 2, 4}, {"raccoon", 3, 1, 2}, {"mouse", 2, 1, 2}, {"crocodile", 4, 1, 2}, {"green_snake", 3, 1, 1}}},
    {"beach", {{"turtle", 8, 2, 4}, {"duck", 2, 1, 3}}},
    {"snowy_beach", {{"snow_fox", 3, 1, 2}, {"rabbit", 3, 1, 2}}},
    {"skylands", {{"aerbunny", 6, 1, 3}, {"moa", 3, 1, 2}, {"phyg", 5, 1, 3}, {"flying_cow", 4, 1, 3}, {"sheepuff", 5, 2, 3}}},
  };
  return S;
}
static const std::unordered_map<std::string, std::vector<SpawnE>>& waterTable() {
  static const std::unordered_map<std::string, std::vector<SpawnE>> S = {
    {"ocean", {{"cod", 10, 3, 6}, {"squid", 6, 1, 3}, {"dolphin", 2, 2, 4}, {"salmon", 3, 3, 5}, {"shark", 1, 1, 1}}},
    {"warm", {{"tropical_fish", 10, 4, 7}, {"pufferfish", 3, 1, 2}, {"cod", 3, 3, 5}, {"dolphin", 2, 2, 3}, {"squid", 2, 1, 2}, {"shark", 2, 1, 1}}},
    {"cold", {{"salmon", 10, 3, 5}, {"cod", 6, 3, 5}, {"squid", 3, 1, 2}}},
    {"lake", {{"salmon", 6, 2, 4}, {"cod", 4, 2, 4}, {"frog", 2, 1, 2}}},
    {"cave", {{"axolotl", 6, 1, 3}, {"glow_squid", 4, 1, 2}}},
  };
  return S;
}
static bool needsWater(const std::string& t) { return t == "duck" || t == "frog" || t == "turtle" || t == "crocodile"; }
static const SpawnE& pickWeighted(const std::vector<SpawnE>& list) {
  float tot = 0; for (auto& e : list) tot += e.w;
  float q = frand() * tot; for (auto& e : list) { q -= e.w; if (q <= 0) return e; }
  return list.back();
}

// ------------------------------------------------------------------ the system
class CreatureSystem : public LevelSystem {
public:
  const char* name() const override { return "creatures"; }
  float spawnAcc = 0, herdAcc = 0, mergeAcc = 0, hiveAcc = 0;
  std::unordered_set<std::string> herdsDone;
  bool herdsLoaded = false;
  struct Colony { ivec3 h; int inside = 0, out = 0; float t = 2, join = 40; };
  std::unordered_map<std::string, Colony> colonies;
  std::unordered_map<ChunkKey, std::vector<ivec3>, ChunkKeyHash> hives;   // per chunk (scanned once, kept up to date by place/break events)
  int evPlace = 0, evBreak = 0; EventBus* bus = nullptr;

  explicit CreatureSystem(Level& lv) {
    reloadCreatureTypes();
    bus = lv.events;
    if (bus) {
      auto onBlock = [this](const json& a, bool placed) {
        if (!a.is_array() || a.size() < 5 || !a[4].is_string()) return true;
        const BlockDef& b = reg().block(reg().blockId(a[4].get<std::string>()));
        if (!b.has("hive")) return true;
        ivec3 p(a[1].get<int>(), a[2].get<int>(), a[3].get<int>());
        auto& v = hives[{dimOfY(p.y), p.x >> 4, p.z >> 4}];
        v.erase(std::remove(v.begin(), v.end(), p), v.end());
        if (placed) v.push_back(p);
        return true;
      };
      evPlace = bus->on("blockPlace", [onBlock](const json& a) { return onBlock(a, true); }, -100);
      evBreak = bus->on("blockBreak", [onBlock](const json& a) { return onBlock(a, false); }, -100);
    }
  }
  ~CreatureSystem() override { if (bus) { bus->off(evPlace); bus->off(evBreak); } }

  static bool rule(Level& lv, const char* k) { return lv.info.rules.value(k, true); }

  void tick(Level& lv, double dtd) override {
    float dt = (float)dtd;
    for (auto& e : lv.entities()) if (auto* p = dynamic_cast<Player*>(e.get())) if (!p->removed) tickPlayerEffects(lv, *p, dt);
    crowdPush(lv, dt);
    spawnAcc += dt;
    if (spawnAcc > 1) { spawnAcc = 0; for (int realm : realmsWithPlayers(lv)) spawnMobs(lv, realm); }
    herdAcc -= dt;
    if (herdAcc <= 0) { herdAcc = 1; herds(lv); }
    bees(lv, std::min(dt, 0.1f));
    mergeAcc += dt;
    if (mergeAcc > 1) { mergeAcc = 0; mergeItems(lv); }
  }
  void save(Level& lv) override {
    json a = json::array(); for (auto& k : herdsDone) a.push_back(k);
    lv.info.data["herdChunks"] = a;
  }

  static std::vector<int> realmsWithPlayers(Level& lv) {
    std::vector<int> r;
    for (auto& e : lv.entities()) if (isPawn(e.get()) && std::find(r.begin(), r.end(), e->realm) == r.end()) r.push_back(e->realm);
    return r;
  }

  // the highest standable point (or -1): skips air and cross/model plants (bestiary.js surfaceY)
  static int surfaceY(World& w, int x, int z, int dim) {
    Chunk* c = w.chunk(dim, x >> 4, z >> 4);
    if (!c || !c->lightReady) return -1;
    const Registry& R = reg();
    int base = dimBase(dim);
    for (int y = std::min(DIM_H - 2, c->maxY); y > 1; y--) {
      BlockId id = w.getBlock(x, base + y, z);
      if (!id || R.tRender[id] == (u8)Render::Cross || R.tRender[id] == (u8)Render::Model) continue;
      return R.tSolid[id] || R.tLiquid[id] ? base + y + 1 : -1;
    }
    return -1;
  }

  void group(Level& lv, int realm, const std::string& type, vec3 at, int k) {
    std::string firstVariant;
    for (int i = 0; i < k; i++) {
      Creature* m = spawnCreature(lv, realm, type, at + vec3(0.5f + (frand() - 0.5f) * 2, 0, 0.5f + (frand() - 0.5f) * 2));
      if (!m) return;
      if (i > 0 && frand() < 0.18f && m->T->farm) m->makeBaby();
      if (i == 0) firstVariant = m->variant;
      else if (!firstVariant.empty() && !m->T->variants.empty() && frand() < 0.6f) m->variant = firstVariant;
    }
  }

  void spawnMobs(Level& lv, int realm) {
    if (!rule(lv, "mobSpawning") || lv.info.difficulty == "peaceful_all") return;
    World& w = lv.world(realm);
    std::vector<Entity*> ps = pawnsOf(lv, realm);
    if (ps.empty()) return;
    Entity* p = ps[(size_t)(frand() * ps.size()) % ps.size()];
    int dim = dimAt(*p);
    // only creatures near this player count toward its caps
    int animals = 0, hostiles = 0, water = 0, ambient = 0, depthsHostile = 0, wisps = 0, cockatrices = 0;
    bool golem = false, werewolf = false, ogre = false;
    for (auto& e : lv.entities()) {
      auto* c = dynamic_cast<Creature*>(e.get());
      if (!c || c->removed || c->realm != realm) continue;
      if (c->type == "cinder_golem") golem = true;
      if (c->type == "werewolf") werewolf = true;
      if (c->type == "ogre") ogre = true;
      if (c->T->hostile && !c->T->boss && dimAt(*c) == DIM_NETHER) depthsHostile++;
      if (c->type == "cinder_wisp" || c->type == "magma_slime") wisps++;
      if (c->type == "cockatrice" && c->alive()) cockatrices++;
      // (the stacked world: creatures in the bands above and below don't fill this band's caps)
      if (std::fabs(c->pos.x - p->pos.x) >= 72 || std::fabs(c->pos.z - p->pos.z) >= 72 || dimAt(*c) != dim) continue;
      const CreatureType& T = *c->T;
      if (T.hostile) { if (!T.boss) hostiles++; }
      else if (T.water) water++;
      else if (T.ambient) ambient++;
      else animals++;
    }
    int n = (int)ps.size();
    int capA = 7 + n * 2, capW = 6 + n * 2, capB = 2 + n, capH = 6 + n * 2;
    bool peaceful = lv.info.difficulty == "peaceful";
    auto ring = [&](float a, float b) { float ang = frand() * TAU, d = a + frand() * (b - a); return ivec2(ifloor(p->pos.x + std::cos(ang) * d), ifloor(p->pos.z + std::sin(ang) * d)); };
    auto crowded = [&](int x, int z, float rad, int max) {
      int k = 0;
      for (auto& e : lv.entities()) if (auto* c = dynamic_cast<Creature*>(e.get())) if (!c->T->hostile && c->realm == realm && dimAt(*c) == dim && std::fabs(c->pos.x - x) < rad && std::fabs(c->pos.z - z) < rad && ++k >= max) return true;
      return false;
    };
    const Registry& R = reg();
    bool OVER = dim == DIM_OVERWORLD, SKY = dim == DIM_AETHER;
    float day = envDay(lv, dim);
    // land animals: by habitat, in daylight on natural ground (herds mostly come with the land)
    if ((OVER || SKY) && animals < capA && frand() < 0.012f) {
      ivec2 xz = ring(32, 56); int x = xz.x, z = xz.y;
      int y = surfaceY(w, x, z, dim);
      BlockId ground = y > 0 ? w.getBlock(x, y - 1, z) : AIR;
      static const std::set<std::string> NATURAL = {"grass", "sky_grass", "snowy_grass", "sand", "red_sand", "desert_sand", "mud", "coarse_dirt", "podzol", "dirt_with_dry_grass", "dirt_with_coniferous_litter", "dirt_with_rainforest_litter", "snow_block", "silver_sand"};
      const std::string& gk = R.block(ground).key;
      bool natural = NATURAL.count(gk) || (gk == "stone" && y - dimBase(dim) > SEA_LEVEL + 20);
      if (y > 0 && natural && !R.tLiquid[ground] && (w.getLight(x, y, z) >> 4) >= 12) {
        std::string hab = SKY ? "skylands" : habitatAt(w, x, z, ground);
        auto it = spawnTable().find(hab); if (it == spawnTable().end()) it = spawnTable().find("plains");
        const SpawnE& e = pickWeighted(it->second);
        const CreatureType* T = creatureType(e.t);
        bool wet = !needsWater(e.t);
        for (int dz = -6; dz <= 6 && !wet; dz += 2) for (int dx = -6; dx <= 6 && !wet; dx += 2) if (R.tLiquid[w.getBlock(x + dx, y - 1, z + dz)] == 1 || R.tLiquid[w.getBlock(x + dx, y, z + dz)] == 1) wet = true;
        if (T && wet && !(peaceful && T->hostile) && !crowded(x, z, 28, 3)) group(lv, realm, e.t, vec3(x, y, z), std::min(3, e.lo + (int)(frand() * (e.hi - e.lo + 1))));
      }
    }
    // water life: oceans, lakes, and the dark water of caves
    if (OVER && water < capW && frand() < 0.12f) {
      ivec2 xz = ring(16, 40); int x = xz.x, z = xz.y;
      int sea = dimBase(DIM_OVERWORLD) + SEA_LEVEL;
      int y0 = std::min(dimBase(dim) + DIM_H - 2, ifloor(p->pos.y) + 12), top = -1;
      if (w.isLoaded(x, y0, z)) for (int y = y0; y > dimBase(dim) + 2 && y > p->pos.y - 30; y--) { BlockId id = w.getBlock(x, y, z); if (R.tLiquid[id] == 1) { top = y; break; } if (R.tSolid[id]) { if (y < sea - 4) continue; break; } }
      if (top > 0) {
        int depth = 0; while (depth < 30 && R.tLiquid[w.getBlock(x, top - depth, z)] == 1) depth++;
        int sky = w.getLight(x, top, z) >> 4;
        float temp = 0; if (Chunk* c = w.chunk(DIM_OVERWORLD, x >> 4, z >> 4)) temp = (c->climate[((z & 15) * 16 + (x & 15)) * 2] - 128) / 127.f;
        bool ocean = depth > 6;
        std::string kind = sky < 6 && top < sea - 6 ? "cave" : ocean ? (temp > 0.3f ? "warm" : temp < -0.1f ? "cold" : "ocean") : "lake";
        if (depth >= 2) {
          const SpawnE& e = pickWeighted(waterTable().at(kind));
          std::string type = e.t;
          if ((type == "dolphin" || type == "squid") && depth < 5) type = "cod";
          if (type == "frog") { if (habitatAt(w, x, z, AIR) == "swamp") group(lv, realm, "frog", vec3(x, top + 1, z), 1); }
          else if (isCreatureType(type) && !(peaceful && creatureType(type)->hostile)) group(lv, realm, type, vec3(x, top - std::min(depth - 1, 1 + (int)(frand() * (depth - 1))), z), e.lo + (int)(frand() * (e.hi - e.lo + 1)));
        }
      }
    }
    // bats in dark caves
    if (OVER && ambient < capB && frand() < 0.08f) {
      ivec2 xz = ring(10, 28); int x = xz.x, z = xz.y;
      int y0 = ifloor(p->pos.y) + (int)(frand() * 20) - 10, sea = dimBase(DIM_OVERWORLD) + SEA_LEVEL;
      if (w.isLoaded(x, y0, z)) for (int y = y0; y > y0 - 10 && y > dimBase(dim) + 3; y--) {
        if (w.getBlock(x, y, z) == AIR && w.getBlock(x, y + 1, z) == AIR && y < sea - 2) {
          u8 l = w.getLight(x, y, z);
          if ((l >> 4) < 4 && (l & 15) < 4) group(lv, realm, "bat", vec3(x, y, z), 1 + (int)(frand() * 3));
          break;
        }
      }
    }
    spawnRealms(lv, realm, *p, dim, day, peaceful);
    if (peaceful) return;
    // the night crowd (monsters.js)
    if (OVER && hostiles < capH) spawnMonsters(lv, realm, *p, ring, day, werewolf, ogre);
    if (dim == DIM_NETHER) spawnDepths(lv, realm, *p, ring, depthsHostile, wisps, golem);
    // skymobs.js: cockatrices come out on the islands at night
    if (SKY && day <= 0.3f && frand() <= 0.02f && cockatrices < 3) {
      float ang = frand() * TAU, d = 24 + frand() * 20; int x = ifloor(p->pos.x + std::cos(ang) * d), z = ifloor(p->pos.z + std::sin(ang) * d);
      for (int y = std::min(dimBase(dim) + DIM_H - 2, ifloor(p->pos.y) + 16); y > ifloor(p->pos.y) - 16 && w.isLoaded(x, y, z); y--) {
        BlockId id = w.getBlock(x, y, z);
        if (!id) continue;
        if (R.tSolid[id] && !w.getBlock(x, y + 1, z) && !w.getBlock(x, y + 2, z) && (w.getLight(x, y + 1, z) & 15) < 8) spawnCreature(lv, realm, "cockatrice", vec3(x + 0.5f, y + 1, z + 0.5f));
        break;
      }
    }
  }

  // nether2.js (zombie pigmen, ghasts, the fortress garrison of wither skeletons), aether.js (aerwhales,
  // zephyrs, swets, aechor plants) and endmobs.js (endermen: thick in the End, now and then in the Depths and
  // on the Overworld at night). Counted near this player, in this band.
  bool hasFort = false; ivec3 fort{0}; float fortT = 0; ivec2 fortAt{1 << 30};   // the nearest fortress (cx, band-local y, cz)
  void spawnRealms(Level& lv, int realm, Entity& p, int dim, float day, bool peaceful) {
    World& w = lv.world(realm);
    const Registry& R = reg();
    int base = dimBase(dim), py = ifloor(p.pos.y);
    auto count = [&](const char* t, float rad) {
      int n = 0;
      for (auto& e : lv.entities()) if (auto* c = dynamic_cast<Creature*>(e.get())) if (c->type == t && c->alive() && !c->removed && c->realm == realm && dimAt(*c) == dim && std::hypot(c->pos.x - p.pos.x, c->pos.z - p.pos.z) < rad) n++;
      return n;
    };
    auto ring = [&](float a, float b) { float ang = frand() * TAU, d = a + frand() * (b - a); return ivec2(ifloor(p.pos.x + std::cos(ang) * d), ifloor(p.pos.z + std::sin(ang) * d)); };
    auto floorAt = [&](int x, int z, int y0, int span) {
      for (int y = y0; y > y0 - span && y > base + 2; y--) if (R.tSolid[w.getBlock(x, y - 1, z)] && !w.getBlock(x, y, z) && !w.getBlock(x, y + 1, z) && !w.getBlock(x, y + 2, z)) return y;
      return -1;
    };
    // (wanderers of their realm, not kept animals: they go when nobody is near, like the night crowd)
    auto add = [&](const char* t, float x, float y, float z) { Creature* m = spawnCreature(lv, realm, t, vec3(x, y, z)); if (m) m->kept = false; return m; };
    if (dim == DIM_NETHER) {
      // the fortress keeps its own garrison
      if ((fortT -= 1) <= 0 || std::abs(fortAt.x - ifloor(p.pos.x)) > 48 || std::abs(fortAt.y - ifloor(p.pos.z)) > 48) {
        fortT = 10; fortAt = ivec2(ifloor(p.pos.x), ifloor(p.pos.z)); hasFort = false;
        if (lv.gen) if (const StructurePlan* s = locateStructure(*lv.gen, DIM_NETHER, "nether_fortress", fortAt.x, fortAt.y, 1))
          if (std::abs(s->cx - fortAt.x) < 24 + 60 && std::abs(s->cz - fortAt.y) < 24 + 60) { hasFort = true; fort = ivec3(s->cx, s->y, s->cz); }
      }
      if (hasFort && !peaceful && count("wither_skeleton", 96) < 5 && frand() < 0.5f) {
        int x = fort.x + (int)std::floor((frand() - 0.5f) * 60), z = fort.z + (int)std::floor((frand() - 0.5f) * 60);
        if (std::hypot(x - p.pos.x, z - p.pos.z) > 12 && w.isLoaded(x, base + fort.y, z)) { int y = floorAt(x, z, base + fort.y + 3, 4); if (y > 0) add("wither_skeleton", x + 0.5f, y, z + 0.5f); }
      }
      if (count("zombie_pigman", 96) < 8 && frand() < 0.22f) {
        ivec2 xz = ring(18, 36);
        if (w.isLoaded(xz.x, py, xz.y)) {
          int y = floorAt(xz.x, xz.y, py + (int)(frand() * 20) - 10, 12);
          if (y > 0) for (int i = 0, k = 2 + (int)(frand() * 3); i < k; i++) {
            int sx = xz.x + (int)std::floor((frand() - 0.5f) * 4), sz = xz.y + (int)std::floor((frand() - 0.5f) * 4), sy = floorAt(sx, sz, y + 2, 5);
            if (sy > 0) add("zombie_pigman", sx + 0.5f, sy, sz + 0.5f);
          }
        }
      }
      if (!peaceful && count("ghast", 96) < 2 && frand() < 0.06f) {
        ivec2 xz = ring(24, 40);
        if (w.isLoaded(xz.x, py, xz.y)) for (int y = py + 12; y > py - 2; y--) {
          bool open = true;
          for (int dy = -1; dy <= 4 && open; dy++) for (ivec2 o : {ivec2(0, 0), ivec2(-2, -2), ivec2(2, -2), ivec2(-2, 2), ivec2(2, 2)}) if (w.getBlock(xz.x + o.x, y + dy, xz.y + o.y)) { open = false; break; }
          if (open) { add("ghast", xz.x + 0.5f, y, xz.y + 0.5f); break; }
        }
      }
    }
    if (dim == DIM_AETHER) {
      auto skyAir = [&](int x, int y, int z, int r) { for (int dy = -r; dy <= r; dy++) for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) if (w.getBlock(x + dx, y + dy, z + dz)) return false; return true; };
      if (frand() < 0.025f && count("aerwhale", 96) < 2) {   // whales in the open sky, well away from you
        ivec2 xz = ring(40, 64); int y = ifloor(clampv(p.pos.y + 6 + frand() * 22, (float)base + 52, (float)base + DIM_H - 14));
        if (w.isLoaded(xz.x, y, xz.y) && skyAir(xz.x, y + 1, xz.y, 3)) if (Creature* m = add("aerwhale", xz.x + 0.5f, y, xz.y + 0.5f)) m->yaw = frand() * TAU;
      }
      if (!peaceful && frand() < 0.018f && count("zephyr", 96) < 2) {
        ivec2 xz = ring(22, 40); int y = ifloor(clampv(p.pos.y + 4 + frand() * 8, (float)base + 48, (float)base + DIM_H - 8));
        if (w.isLoaded(xz.x, y, xz.y) && skyAir(xz.x, y + 1, xz.y, 1)) add("zephyr", xz.x + 0.5f, y, xz.y + 0.5f);
      }
      static BlockId skyGrass = R.blockId("sky_grass");
      auto ground = [&](const char* t, float lo, float hi) {
        ivec2 xz = ring(lo, hi);
        if (!w.isLoaded(xz.x, py, xz.y)) return;
        for (int y = std::min(base + DIM_H - 3, py + 14); y > std::max(base + 30, py - 20); y--) {
          BlockId id = w.getBlock(xz.x, y, xz.y); if (!id) continue;
          if (id == skyGrass && !w.getBlock(xz.x, y + 1, xz.y) && !w.getBlock(xz.x, y + 2, xz.y)) add(t, xz.x + 0.5f, y + 1, xz.y + 0.5f);
          return;
        }
      };
      if (!peaceful && frand() < 0.035f && count("swet", 96) + count("golden_swet", 96) < 4) ground(frand() < 0.2f ? "golden_swet" : "swet", 16, 34);
      if (!peaceful && frand() < 0.02f && count("aechor_plant", 96) < 3) ground("aechor_plant", 18, 40);
    }
    // endermen
    if (!peaceful) {
      int cap = dim == DIM_END ? 12 : 2;
      float chance = dim == DIM_END ? 0.7f : dim == DIM_NETHER ? 0.04f : dim == DIM_OVERWORLD && day < 0.42f ? 0.05f : 0;
      if (chance > 0 && frand() <= chance && count("enderman", 72) < cap) {
        ivec2 xz = ring(18, 44);
        if (w.isLoaded(xz.x, py, xz.y)) {
          int from = std::min(base + DIM_H - 4, py + (dim == DIM_END ? 30 : 12)), to = std::max(base + 2, py - (dim == DIM_END ? 40 : 16));
          for (int y = from; y >= to; y--) {
            int x = xz.x, z = xz.y;
            if (!R.tSolid[w.getBlock(x, y - 1, z)] || R.tSolid[w.getBlock(x, y, z)] || R.tSolid[w.getBlock(x, y + 1, z)] || R.tSolid[w.getBlock(x, y + 2, z)] || R.tLiquid[w.getBlock(x, y, z)]) continue;
            if (dim == DIM_OVERWORLD && (w.getLight(x, y, z) & 15) > 0) break;
            add("enderman", x + 0.5f, y, z + 0.5f);
            break;
          }
        }
      }
    }
  }

  template <class Ring>
  void spawnMonsters(Level& lv, int realm, Entity& p, Ring ring, float day, bool werewolf, bool ogre) {
    World& w = lv.world(realm);
    const Registry& R = reg();
    struct NE { const char* t; float w; };
    static const std::unordered_map<std::string, std::vector<NE>> NIGHT = {
      {"base", {{"ghoul", 30}, {"skeleton", 24}, {"spider", 22}, {"fuseling", 18}, {"wraith", 1.2f}, {"ogre", 0.8f}}},
      {"desert", {{"husk", 30}, {"ghoul", 4}, {"skeleton", 20}, {"spider", 18}, {"fuseling", 16}, {"scorpion", 10}, {"wraith", 1}}},
      {"snowy", {{"ghoul", 28}, {"stray", 26}, {"spider", 16}, {"fuseling", 16}, {"wraith", 1.5f}}},
      {"swamp", {{"ghoul", 26}, {"skeleton", 18}, {"spider", 18}, {"fuseling", 14}, {"witch", 7}, {"slime", 8}, {"drowned", 6}}},
      {"forest", {{"ghoul", 30}, {"skeleton", 24}, {"spider", 22}, {"fuseling", 18}, {"raider", 0.3f}, {"werewolf", 1.2f}, {"wraith", 1.2f}, {"ogre", 0.8f}}},
      {"open", {{"ghoul", 30}, {"skeleton", 24}, {"spider", 22}, {"fuseling", 18}, {"raider", 0.4f}, {"wraith", 1}, {"ogre", 1}}},
    };
    auto habGroup = [](const std::string& h) -> std::string {
      if (h == "desert") return "desert";
      if (h.find("snow") != std::string::npos) return "snowy";
      if (h == "swamp") return "swamp";
      if (h.find("forest") != std::string::npos || h.find("taiga") != std::string::npos || h.find("jungle") != std::string::npos) return "forest";
      if (h.find("plains") != std::string::npos || h.find("savanna") != std::string::npos || h.find("mountains") != std::string::npos) return "open";
      return "base";
    };
    auto pickW = [](const std::vector<NE>& list) { float t = 0; for (auto& e : list) t += e.w; float q = frand() * t; for (auto& e : list) { q -= e.w; if (q <= 0) return std::string(e.t); } return std::string(list[0].t); };
    auto spawnOne = [&](const std::string& t, int x, int y, int z) {
      if (globalCreatureHooks().naturalSpawn && !globalCreatureHooks().naturalSpawn(lv, realm, t, vec3(x + 0.5f, y, z + 0.5f))) return;   // villagers.js: not inside a village
      Creature* m = spawnCreature(lv, realm, t, vec3(x + 0.5f, y, z + 0.5f));
      if (!m) return;
      if (t == "slime" || t == "magma_slime") m->setSize(frand() < 0.6f ? 2 : 1);
      if (t == "raider") {   // a band: a leader and two to four more
        for (int i = 0, n = 2 + (int)(frand() * 3); i < n; i++)
          if (Creature* o = spawnCreature(lv, realm, "raider", vec3(x + 0.5f + (frand() - 0.5f) * 3, y, z + 0.5f + (frand() - 0.5f) * 3))) { o->leader = m->id; if (frand() < 0.35f) o->heldItem = "stone_axe"; }
      }
      if (t == "drowned" && frand() < 0.2f) m->heldItem = "iron_sword";
    };
    // at night, out in the open: only where no torch or lamp reaches at all (light 0)
    if (day < 0.42f && frand() < 0.3f) {
      ivec2 xz = ring(24, 44); int x = xz.x, z = xz.y;
      int y = surfaceY(w, x, z, DIM_OVERWORLD);
      if (y > 0 && (w.getLight(x, y, z) & 15) == 0) {
        BlockId ground = w.getBlock(x, y - 1, z);
        if (R.tLiquid[ground]) {   // the drowned come up out of deep water
          int depth = 0; while (depth < 8 && R.tLiquid[w.getBlock(x, y - 1 - depth, z)] == 1) depth++;
          if (depth >= 3 && frand() < 0.35f) spawnOne("drowned", x, y - depth, z);
        } else {
          std::string t = pickW(NIGHT.at(habGroup(habitatAt(w, x, z, ground))));
          if (t == "werewolf" && werewolf) t = "ghoul";
          if (t == "ogre" && ogre) t = "skeleton";
          if (t == "drowned") t = "ghoul";
          spawnOne(t, x, t == "wraith" ? y + 1 : y, z);
        }
      }
    }
    // underground, in the dark
    if (frand() < 0.2f) {
      ivec2 xz = ring(14, 30); int x = xz.x, z = xz.y;
      int y0 = ifloor(p.pos.y) + (int)(frand() * 24) - 12;
      if (w.isLoaded(x, y0, z)) for (int y = y0; y > y0 - 8 && y > dimBase(DIM_OVERWORLD) + 2; y--) {
        if (R.tSolid[w.getBlock(x, y - 1, z)] && w.getBlock(x, y, z) == AIR && w.getBlock(x, y + 1, z) == AIR) {
          u8 l = w.getLight(x, y, z);
          if ((l >> 4) * day < 6 && (l & 15) == 0 && (l >> 4) < 12) {
            int ly = y - dimBase(DIM_OVERWORLD);
            float q = frand();
            // the original's y < 40 / y < 32 in a 128-tall world with sea 48: scaled to sea 64
            std::string t = ly < 40 + 16 && q < 0.12f ? "slime" : ly < 32 + 16 && q < 0.26f ? "cave_spider" : "";
            if (t.empty()) { float k = frand(); t = k < 0.34f ? "ghoul" : k < 0.58f ? "skeleton" : k < 0.8f ? "spider" : "fuseling"; }
            if (t == "cave_spider" && frand() < 0.5f) spawnOne(t, x, y, z);   // they come in twos and threes
            spawnOne(t, x, y, z);
          }
          break;
        }
      }
    }
  }

  template <class Ring>
  void spawnDepths(Level& lv, int realm, Entity& p, Ring ring, int hostiles, int wisps, bool golem) {
    World& w = lv.world(realm);
    const Registry& R = reg();
    int base = dimBase(DIM_NETHER);
    auto open3 = [&](int x, int y, int z) { return R.tSolid[w.getBlock(x, y - 1, z)] && w.getBlock(x, y, z) == AIR && w.getBlock(x, y + 1, z) == AIR; };
    if (hostiles < 7 && frand() < 0.5f) {
      ivec2 xz = ring(16, 34); int y0 = ifloor(p.pos.y) + (int)(frand() * 24) - 12;
      if (w.isLoaded(xz.x, y0, xz.y)) for (int y = y0; y > y0 - 10 && y > base + 2; y--) if (open3(xz.x, y, xz.y)) { if ((w.getLight(xz.x, y, xz.y) & 15) < 11) spawnCreature(lv, realm, "charred_ghoul", vec3(xz.x + 0.5f, y, xz.y + 0.5f)); break; }
    }
    if (!golem && frand() < 0.004f) {
      ivec2 xz = ring(20, 30);
      for (int y = ifloor(p.pos.y) + 8; y > p.pos.y - 8 && w.isLoaded(xz.x, y, xz.y); y--)
        if (open3(xz.x, y, xz.y) && !w.getBlock(xz.x, y + 2, xz.y)) { summonGolem(lv, realm, vec3(xz.x + 0.5f, y, xz.y + 0.5f), &p); break; }
    }
    // monsters.js: wisps and magma slimes besides the charred ghouls
    if (wisps >= 6 || frand() > 0.3f) return;
    ivec2 xz = ring(16, 34); int y0 = ifloor(p.pos.y) + (int)(frand() * 20) - 10;
    if (w.isLoaded(xz.x, y0, xz.y)) for (int y = y0; y > y0 - 10 && y > base + 2; y--) if (open3(xz.x, y, xz.y)) {
      if (frand() < 0.45f) spawnCreature(lv, realm, "cinder_wisp", vec3(xz.x + 0.5f, y + 1.5f, xz.y + 0.5f));
      else if (Creature* m = spawnCreature(lv, realm, "magma_slime", vec3(xz.x + 0.5f, y, xz.y + 0.5f))) m->setSize(frand() < 0.6f ? 2 : 1);
      break;
    }
  }

  // ---- herds come with the land: once per piece of ground, the first time anyone sees it ----
  void herds(Level& lv) {
    if (!herdsLoaded) { herdsLoaded = true; if (lv.info.data.contains("herdChunks")) for (auto& k : lv.info.data["herdChunks"]) if (k.is_string()) herdsDone.insert(k.get<std::string>()); }
    if (!rule(lv, "mobSpawning")) return;
    const Registry& R = reg();
    for (int realm : realmsWithPlayers(lv)) {
      World& w = lv.world(realm);
      std::vector<Entity*> ps = pawnsOf(lv, realm);
      std::vector<Chunk*> todo;
      for (auto& [k, ch] : w.chunks()) {
        if ((k.dim != DIM_OVERWORLD && k.dim != DIM_AETHER) || !ch->lightReady) continue;
        bool seen = false;
        for (Entity* p : ps) if (std::fabs(k.cx * 16 + 8 - p->pos.x) < 72 && std::fabs(k.cz * 16 + 8 - p->pos.z) < 72) seen = true;
        if (!seen) continue;
        std::string key = std::to_string(realm) + ":" + std::to_string(k.dim) + ":" + std::to_string(k.cx) + ":" + std::to_string(k.cz);
        if (herdsDone.count(key)) continue;
        herdsDone.insert(key); todo.push_back(ch.get());
      }
      for (Chunk* ch : todo) {
        u32 hsh = (u32)lv.info.seed * 2654435761u ^ (u32)ch->cx * 73856093u ^ (u32)ch->cz * 19349663u ^ 4242u;
        auto r = [&hsh] { hsh ^= hsh << 13; hsh ^= hsh >> 17; hsh ^= hsh << 5; return (hsh & 0xFFFFFF) / 16777216.f; };
        r(); r();
        bool sky = ch->dim == DIM_AETHER;
        if (r() > (sky ? 0.12f : 0.09f)) continue;
        for (int tries = 0; tries < 6; tries++) {
          int x = ch->cx * 16 + (int)(r() * 16), z = ch->cz * 16 + (int)(r() * 16), base = dimBase(ch->dim);
          int y = base + std::min(DIM_H - 2, ch->maxY);
          while (y > base + 1 && (!w.getBlock(x, y, z) || R.tRender[w.getBlock(x, y, z)] == (u8)Render::Cross)) y--;
          BlockId ground = w.getBlock(x, y, z), above = w.getBlock(x, y + 1, z);
          if (!ground || R.tLiquid[ground] || R.tLeaves[ground] || !R.tSolid[ground] || (above && R.tRender[above] != (u8)Render::Cross)) continue;
          if ((w.getLight(x, y + 1, z) >> 4) < 12) continue;
          std::string hab = sky ? "skylands" : habitatAt(w, x, z, ground);
          auto it = spawnTable().find(hab); if (it == spawnTable().end()) it = spawnTable().find("plains");
          float tot = 0; for (auto& e : it->second) tot += e.w;
          float q = r() * tot; const SpawnE* pick = &it->second.back();
          for (auto& e : it->second) { q -= e.w; if (q <= 0) { pick = &e; break; } }
          const CreatureType* T = creatureType(pick->t);
          if (!T || needsWater(pick->t) || std::string(pick->t) == "mouse") break;
          int n = std::min(4, pick->lo + (int)(r() * (pick->hi - pick->lo + 1)));
          std::string firstVariant;
          for (int i = 0; i < n; i++) {
            Creature* m = spawnCreature(lv, realm, pick->t, vec3(x + 0.5f + (r() - 0.5f) * 3, y + 1, z + 0.5f + (r() - 0.5f) * 3));
            if (!m) break;
            if (i > 0 && r() < 0.2f && m->T->farm) m->makeBaby();
            if (i == 0) firstVariant = m->variant; else if (!firstVariant.empty() && !m->T->variants.empty() && r() < 0.6f) m->variant = firstVariant;
          }
          break;
        }
      }
    }
  }

  // ---- bees: little things, a swarm to a nest ----
  void scanHives(World& w, Chunk& c) {
    ChunkKey k{c.dim, c.cx, c.cz};
    if (hives.count(k)) return;
    auto& v = hives[k];
    static std::vector<u8> isHive;
    if (isHive.size() != reg().blocks.size()) { isHive.assign(reg().blocks.size(), 0); for (auto& b : reg().blocks) if (b.has("hive")) isHive[b.id] = 1; }
    for (int sy = 0; sy < SECS; sy++) {
      const Section* s = c.section(sy);
      if (!s) continue;
      for (int i = 0; i < 4096; i++) if (s->blocks[i] < isHive.size() && isHive[s->blocks[i]]) v.push_back({c.cx * 16 + (i & 15), dimBase(c.dim) + sy * 16 + (i >> 8), c.cz * 16 + ((i >> 4) & 15)});
    }
  }
  static std::string hkey(ivec3 h) { return std::to_string(h.x) + "," + std::to_string(h.y) + "," + std::to_string(h.z); }
  void bees(Level& lv, float dt) {
    hiveAcc -= dt;
    bool scan = hiveAcc <= 0; if (scan) hiveAcc = 2;
    static BlockId nest = reg().blockId("bee_nest"), hive = reg().blockId("beehive");
    for (int realm : realmsWithPlayers(lv)) {
      World& w = lv.world(realm);
      std::vector<Entity*> ps = pawnsOf(lv, realm);
      if (scan) for (auto& [k, ch] : w.chunks()) if (k.dim == DIM_OVERWORLD || k.dim == DIM_AETHER) for (Entity* p : ps) if (std::fabs(k.cx * 16 + 8 - p->pos.x) < 56 && std::fabs(k.cz * 16 + 8 - p->pos.z) < 56) { scanHives(w, *ch); break; }
      std::unordered_map<std::string, int> out;
      for (auto& e : lv.entities()) if (auto* b = dynamic_cast<Creature*>(e.get())) if (b->type == "bee" && b->alive() && b->hasHive && b->realm == realm) out[hkey(b->hive)]++;
      bool day = lv.info.data.value("rain", 0.f) <= 0.3f;
      for (auto& [k, list] : hives) {
        if (!w.chunk(k.dim, k.cx, k.cz)) continue;
        for (ivec3 h : list) {
          BlockId id = w.getBlock(h.x, h.y, h.z);
          if (!reg().block(id).has("hive")) continue;
          bool near = false; for (Entity* p : ps) if (std::hypot(h.x - p->pos.x, h.z - p->pos.z) < 48) near = true;
          if (!near) continue;
          bool isDay = day && envDay(lv, dimOfY(h.y)) > 0.45f;
          std::string key = hkey(h);
          auto it = colonies.find(key);
          if (it == colonies.end()) { Colony c; c.h = h; c.inside = id == nest ? 3 + (int)(frand() * 3) : 0; c.t = 1 + frand() * 3; it = colonies.emplace(key, c).first; }
          Colony& c = it->second;
          c.out = out[key];
          if (id == hive && c.inside + c.out < 4 && isDay && (c.join -= dt) <= 0) {   // a swarm moves into a built hive once there are flowers near
            c.join = 45 + frand() * 45; ivec3 f;
            if (beeFlower(w, vec3(h), 8, 30, f)) c.inside++;
          }
          if (!isDay || c.inside <= 0 || (c.t -= dt) > 0) continue;
          c.t = 2 + frand() * 5;
          vec3 f = hiveFront(w, h);
          if (reg().tSolid[w.getBlock(ifloor(f.x), ifloor(f.y), ifloor(f.z))]) continue;
          if (Creature* b = spawnCreature(lv, realm, "bee", f - vec3(0, 0.1f, 0))) { b->hive = h; b->hasHive = true; b->beeState = 0; b->hasSting = true; b->data["stingInit"] = true; }
          c.inside--;
        }
      }
    }
  }
  void beeHome(Creature& b) { auto it = colonies.find(hkey(b.hive)); if (it != colonies.end()) it->second.inside++; }

  // ---- crowd.js: bodies don't share space ----
  void crowdPush(Level& lv, float dt) {
    if (dt <= 0) return;
    struct Body { Entity* e; float r, h, mass; bool player; int i; };
    std::vector<Body> bodies;
    for (auto& e : lv.entities()) {
      if (e->removed) continue;
      if (auto* c = dynamic_cast<Creature*>(e.get())) {
        if (!c->alive() || c->ghost || c->riddenBy || c->jockeyOf || c->jockeyRider || c->T->noPush) continue;
        float r = c->w > 0 ? c->w : 0.3f, h = c->h > 0 ? c->h : 1;
        bodies.push_back({c, r, h, std::max(0.05f, r * r * h * 4), false, 0});
      } else if (auto* p = dynamic_cast<Player*>(e.get())) {
        if (p->dead || p->riding || (p->flying && p->creative()) || p->mode == "spectator") continue;
        bodies.push_back({p, 0.3f, p->sneaking ? 1.5f : 1.8f, 0.65f, true, 0});
      }
    }
    if (bodies.size() < 2) return;
    std::unordered_map<i64, std::vector<int>> cell;
    auto key = [](int realm, int x, int z) { return ((i64)realm << 48) ^ ((i64)(x & 0xffffff) << 24) ^ (i64)(z & 0xffffff); };
    for (size_t i = 0; i < bodies.size(); i++) { bodies[i].i = (int)i; cell[key(bodies[i].e->realm, ifloor(bodies[i].e->pos.x / 2), ifloor(bodies[i].e->pos.z / 2))].push_back((int)i); }
    auto nudge = [&](Body& b, float nx, float nz, float amt, float dv) {
      Entity* e = b.e;
      e->vel.x += nx * dv; e->vel.z += nz * dv;
      if (!b.player && amt > 0) {
        vec3 to = e->pos + vec3(nx * amt, 0, nz * amt);
        World& w = lv.world(e->realm);
        std::vector<AABB> tmp; bool free = true;
        AABB box{to + vec3(-b.r * 0.9f, 0.02f, -b.r * 0.9f), to + vec3(b.r * 0.9f, std::min(b.h, 1.8f) - 0.02f, b.r * 0.9f)};
        for (int y = ifloor(box.lo.y) - 1; y <= ifloor(box.hi.y) && free; y++) for (int z = ifloor(box.lo.z); z <= ifloor(box.hi.z) && free; z++) for (int x = ifloor(box.lo.x); x <= ifloor(box.hi.x) && free; x++) {
          tmp.clear(); blockBoxes(w, x, y, z, tmp); for (auto& bb : tmp) if (bb.overlaps(box)) { free = false; break; }
        }
        if (free) e->pos = to;
      }
    };
    for (auto& b : bodies) {
      int cx = ifloor(b.e->pos.x / 2), cz = ifloor(b.e->pos.z / 2);
      for (int gz = cz - 1; gz <= cz + 1; gz++) for (int gx = cx - 1; gx <= cx + 1; gx++) {
        auto it = cell.find(key(b.e->realm, gx, gz)); if (it == cell.end()) continue;
        for (int oi : it->second) {
          Body& o = bodies[oi];
          if (o.i <= b.i || (b.player && o.player)) continue;
          Entity *a = b.e, *c = o.e;
          if (a->realm != c->realm) continue;
          if (a->pos.y + b.h <= c->pos.y + 0.05f || c->pos.y + o.h <= a->pos.y + 0.05f) continue;
          float dx = c->pos.x - a->pos.x, dz = c->pos.z - a->pos.z, d = std::hypot(dx, dz);
          float mn = (b.r + o.r) * 0.92f;
          if (d >= mn) continue;
          float depth = (mn - d) / mn;
          if (d < 0.001f) { float ang = frand() * TAU; dx = std::cos(ang); dz = std::sin(ang); d = 1; }
          float nx = dx / d, nz = dz / d, tot = b.mass + o.mass, sa = o.mass / tot, sc = b.mass / tot;
          float dv = std::min(6.f, depth * 30) * dt, shift = std::min(0.15f, depth * mn * 0.5f) * std::min(1.f, dt * 20);
          nudge(b, -nx, -nz, shift * sa, dv * sa * (b.player ? 0.6f : 1));
          nudge(o, nx, nz, shift * sc, dv * sc * (o.player ? 0.6f : 1));
        }
      }
    }
  }

  // ---- dropped items next to each other become one stack ----
  void mergeItems(Level& lv) {
    std::vector<Entity*> items;
    for (auto& e : lv.entities()) if (e->type == "item" && !e->removed) items.push_back(e.get());
    for (size_t i = 0; i < items.size(); i++) for (size_t j = i + 1; j < items.size(); j++) {
      Entity *a = items[i], *b = items[j];
      if (a->removed || b->removed || a->realm != b->realm) continue;
      if (std::fabs(a->pos.x - b->pos.x) >= 0.8f || std::fabs(a->pos.y - b->pos.y) >= 0.8f || std::fabs(a->pos.z - b->pos.z) >= 0.8f) continue;
      ItemStack sa = ItemStack::fromJson(a->data.value("stack", json())), sb = ItemStack::fromJson(b->data.value("stack", json()));
      if (!sa.canMerge(sb) || sa.n + sb.n > sa.maxStack()) continue;
      sa.n += sb.n; a->data["stack"] = sa.toJson(); b->removed = true;
    }
  }
};

void beeReturned(Level& lv, Creature& b) { if (auto* s = lv.system<CreatureSystem>()) s->beeHome(b); }

void summonGolem(Level& lv, int realm, vec3 at, Entity* near) {
  Creature* g = spawnCreature(lv, realm, "cinder_golem", at);
  if (!g) return;
  g->state = "spawning"; g->stateT = 2.2f; g->invulnT = 2.2f;
  if (near) g->yaw = std::atan2(-(near->pos.x - at.x), -(near->pos.z - at.z));
  lv.fx->sound("roar", at, 60);
  for (Entity* q : pawnsOf(lv, realm)) if (glm::distance(q->pos, at) < 32) lv.fx->shake(q->id, 0.5f);
  lv.fx->message(0, "The Cinder Golem awakens!", "boss");
  for (int i = 0; i < 30; i++) flame(lv, at + vec3((frand() - 0.5f) * 2, frand() * 3, (frand() - 0.5f) * 2));
}

void registerCreatures() {
  static bool done = false;
  if (done) return;
  done = true;
  registerCreatureEntities();
  registerSpawners();
  registerPocket();
  registerUsables();
  // every MOB_TYPES key is a creature (the "" fallback catches types added later by data packs and mods)
  registerEntityFactory("", [](const std::string& t) -> std::unique_ptr<Entity> { if (!isCreatureType(t)) return nullptr; return std::make_unique<Creature>(t); });
  registerLevelSystem([](Level& lv) { return std::make_shared<CreatureSystem>(lv); });
}

} // namespace dq
