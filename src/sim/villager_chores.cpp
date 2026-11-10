// sim/villager_chores.cpp — a living, working village. In their working hours (sim/villager_ai.cpp brainInner) every
// trade does its own real work, and what they make goes into the village's stores (villageRec(...)["stock"]):
//   farmer      reaps the ripe crops on their farm and sows them again; carries the wheat to the windmill, the rest
//               to the hall
//   miller      feeds the windmill's millstone the village's wheat and bags the flour (the sails turn it with the wind,
//               sim/machines.cpp; an old windmill with no stones in it: a hand quern, slowly)
//   baker       fetches the flour from the mill, bakes it into bread at the stall
//   lumberjack  fells a grown tree out past the houses, plants a sapling in its place; the logs to the stores
//   miner       works the nearest bare stone: cobblestone, coal, now and then iron, copper or flint
//   fisherman   fishes from the nearest shore: cod and salmon
//   rancher     tends the animals in the pen: eggs and feathers, beef, pork; the shepherd shears the sheep: wool
//   butcher     cooks the meat; mason dresses cobblestone into bricks; the smiths smelt ore into iron; the fletcher
//               makes sticks of logs and arrows of sticks, flint and feathers; the innkeeper cooks a vegetable stew
//   builder     puts back what an explosion blew out of the village, block by block, the way it was
// Shops sell what the village has made (a baker with no flour has no bread); what you sell them goes to the stores
// (sim/villager_quests.cpp).
#include "gen/town_mine.h"
#include "sim/villagers_internal.h"
#include "sim/creature.h"
#include "sim/player.h"
#include "core/registry.h"
#include "world/world.h"
#include "common/noise.h"
#include <unordered_set>
#include <cstdlib>

namespace dq {

json& villageStock(Level& lv, const Village& v) {
  json& r = villageRec(lv, v.id);
  if (!r["stock"].is_object()) {   // a village has something put by when you first come
    r["stock"] = json{{"wheat", 8}, {"flour", 8}, {"bread", 18}, {"cobblestone", 32}, {"stone_bricks", 16}, {"coal", 8}, {"iron_ore", 4},
                      {"cooked_beef", 6}, {"cooked_porkchop", 6}, {"cooked_chicken", 6}, {"wool", 8}, {"cod", 6}, {"salmon", 4}, {"arrow", 16},
                      {"egg", 6}, {"vegetable_stew", 2}};
  }
  return r["stock"];
}
bool villageMakes(const std::string& k) {
  static const std::unordered_set<std::string> M = {"bread", "flour", "cooked_beef", "cooked_porkchop", "cooked_chicken", "cooked_mutton", "wool", "cod", "salmon",
    "stone_bricks", "arrow", "coal", "iron_ore", "copper_ore", "cobblestone", "vegetable_stew", "egg", "iron_ingot", "stick"};
  return M.count(k) > 0;
}
bool villageUses(const std::string& k) {
  static const std::unordered_set<std::string> U = {"wheat", "flour", "cobblestone", "coal", "iron_ore", "beef", "porkchop", "chicken", "mutton", "carrot", "potato",
    "beetroot", "feather", "flint", "stick", "log"};
  return U.count(k) > 0 || (k.size() > 4 && k.compare(k.size() - 4, 4, "_log") == 0);
}

// ---- harm to a village: remembered, for its builders ----
VillageRef villageForDamage(Level& lv, int realm, vec3 at) {
  if (realm != REALM_MAIN || lv.isRemote()) return nullptr;
  return villageAtPos(lv, realm, at, 12);
}
bool villageDamage(Level& lv, const Village& v, int x, int y, int z, BlockId id, u8 meta) {
  const Registry& R = reg();
  if (!id || R.tLiquid[id] || R.block(id).has("townGate") || R.block(id).has("isLeaves") || R.block(id).has("fire")) return false;
  if (std::hypot(x - v.x, z - v.z) > v.r + 10) return false;
  json& r = villageRec(lv, v.id);
  json& D = r["damage"];
  if (!D.is_array()) D = json::array();
  if (D.size() >= 600) return false;
  // [x, y, z, block, meta, carve, coat, goods]: the chiselled counter half-blocks, the rug on a floor, the goods of a
  // shelf, a table's pile, a stand, a rack, a frame, a bookcase, a pot, a lectern (not a chest's: those spill)
  World& w = lv.world(REALM_MAIN);
  const u8 carve = w.getCarve(x, y, z), coat = w.getCoat(x, y, z);
  json goods = nullptr;
  if (const json* e = w.blockEntity(x, y, z); e && e->is_object()) {
    static const std::unordered_set<std::string> DECOR = {"pile", "shelf", "bookcase", "frame", "rack", "stand", "pot", "lectern"};
    if (DECOR.count(e->value("type", std::string()))) goods = *e;
  }
  json rec = json::array({x, y, z, R.block(id).key, meta});
  if (carve || coat || !goods.is_null()) { rec.push_back(carve); rec.push_back(coat); rec.push_back(goods); }
  D.push_back(rec);
  if (goods.is_null()) return false;
  w.setBlockEntity(x, y, z, nullptr);   // (kept with the record: put back with the block)
  return true;
}

namespace {
float rnd01() { static thread_local u32 s = 0x9E3779B9u; s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) / 16777216.f; }
int stockOf(json& S, const std::string& k) { return S.value(k, 0); }
void stockAdd(json& S, const std::string& k, int n) { if (!k.empty()) S[k] = std::max(0, S.value(k, 0) + n); }
// any log in the stores (the lumberjack brings whatever the trees here are)
std::string anyLog(json& S) { for (auto& [k, v] : S.items()) if (v.is_number() && v.get<int>() > 0 && (k == "log" || (k.size() > 4 && k.compare(k.size() - 4, 4, "_log") == 0))) return k; return ""; }

const VBld* nearestOf(const Village& v, const char* type, vec3 to, const char* animals = nullptr, bool notAnimals = false) {
  const VBld* best = nullptr; float bd = 1e9f;
  for (const VBld& b : v.blds) if (b.type == type) {
    if (animals && ((b.animals == animals) == notAnimals)) continue;
    const float d = std::hypot((b.x0 + b.x1) * 0.5f - to.x, (b.z0 + b.z1) * 0.5f - to.z);
    if (d < bd) { bd = d; best = &b; }
  }
  return best;
}
bool inBuilding(const Village& v, int x, int z, int pad) {
  for (const VBld& b : v.blds) if (x >= std::min(b.x0, b.x1) - pad && x <= std::max(b.x0, b.x1) + pad && z >= std::min(b.z0, b.z1) - pad && z <= std::max(b.z0, b.z1) + pad) return true;
  return false;
}
bool ripe(BlockId id, u8 meta) {
  const BlockDef& d = reg().block(id);
  if (!d.props.contains("crop") || !d.props["crop"].is_object()) return false;
  return meta >= d.props["crop"].value("stages", 8) - 1;
}
std::string cropItem(const BlockDef& d) {
  if (d.key == "wheat_crop") return "wheat";
  if (d.key == "beetroot_crop") return "beetroot";
  return d.props["crop"].value("drop", std::string());
}
vec3 doorOf(const VBld& b) { return vec3(b.door) + vec3(0.5f, 0, 0.5f); }
bool by(const Creature& e, vec3 at, float r) { return std::hypot(at.x - e.pos.x, at.z - e.pos.z) < r && std::fabs(at.y - e.pos.y) < 2.6f; }
void holding(Creature& e, const NpcMind& M, const std::string& job) {
  if (!M.carry.empty() && M.carryN > 0 && reg().item(M.carry)) { e.heldItem = M.carry; return; }
  const json& J = VD()["jobs"];
  e.heldItem = J.contains(job) && J[job].contains("held") && J[job]["held"].is_string() ? J[job]["held"].get<std::string>() : "";
  if (!e.heldItem.empty() && !reg().item(e.heldItem)) e.heldItem.clear();
}
bool millstoneIn(World& w, const VBld& b, ivec3& out) {
  static const BlockId MS = reg().blockId("millstone");
  if (!MS) return false;
  for (int y = b.y; y <= b.y + 12; y++) for (int z = std::min(b.z0, b.z1); z <= std::max(b.z0, b.z1); z++) for (int x = std::min(b.x0, b.x1); x <= std::max(b.x0, b.x1); x++)
    if (w.isLoaded(x, y, z) && w.getBlock(x, y, z) == MS) { out = ivec3(x, y, z); return true; }
  return false;
}
// the top of a column near the village (the first block down from above the roofs), or false
bool topAt(World& w, const Village& v, int x, int z, int& y, BlockId& id) {
  const Registry& R = reg();
  static const BlockId SNOW = R.blockId("snow_layer");
  for (y = v.y + 28; y > v.y - 16; y--) {
    if (!w.isLoaded(x, y, z)) return false;
    id = w.getBlock(x, y, z);
    if (!id || id == SNOW) continue;   // (the snow lying on a tree's crown isn't its top)
    if (R.tLiquid[id] || !R.tReplaceable[id]) return true;   // water stops it (it's "replaceable": it looked through lakes)
  }
  return false;
}
// somewhere to stand beside (x, y, z): a free cell with ground under it
bool standBeside(World& w, int x, int y, int z, ivec3& out) {
  const Registry& R = reg();
  static const int D[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
  for (int dy = 0; dy >= -2; dy--) for (auto& d : D) {
    const int sx = x + d[0], sz = z + d[1], sy = y + dy;
    if (!R.tSolid[w.getBlock(sx, sy, sz)] && !R.tSolid[w.getBlock(sx, sy + 1, sz)] && !R.tLiquid[w.getBlock(sx, sy, sz)] && R.tSolid[w.getBlock(sx, sy - 1, sz)]) { out = ivec3(sx, sy, sz); return true; }
  }
  return false;
}
bool isLog(BlockId id) { const std::string& k = reg().block(id).key; return k == "log" || (k.size() > 4 && k.compare(k.size() - 4, 4, "_log") == 0 && k.rfind("stripped", 0) != 0); }
bool isSoil(BlockId id) { const std::string& k = reg().block(id).key; return k == "grass" || k == "dirt" || k == "podzol" || k == "coarse_dirt" || k == "snowy_grass" || k == "mud" || k == "rooted_dirt"; }
bool stoneLike(BlockId id) {
  const std::string& k = reg().block(id).key;
  if (k.find("brick") != std::string::npos || k.find("cobble") != std::string::npos || k.find("polished") != std::string::npos || k.find("slab") != std::string::npos || k.find("stairs") != std::string::npos) return false;
  for (const char* s : {"stone", "granite", "andesite", "diorite", "deepslate", "tuff", "slate", "basalt", "limestone", "calcite"}) if (k.find(s) != std::string::npos) return true;
  return false;
}
// The top of a column (the snow on it looked through), wherever the land is: the world's own surface, not a band
// round the square (the jigsaw villages climb hills: trees and shores beyond the walls were out of reach of it)
bool surfaceAt(World& w, int x, int z, int near, int& y, BlockId& id) {
  static const BlockId SNOW = reg().blockId("snow_layer");
  if (!w.isLoaded(x, near, z)) return false;
  y = w.surfaceY(x, z, DIM_OVERWORLD);
  if (y < 0) return false;
  id = w.getBlock(x, y, z);
  if (id == SNOW) id = w.getBlock(x, --y, z);
  return std::abs(y - near) < 40;
}
// where to look: every other look out from where they stand, the rest round the village's edge and a little beyond it
std::pair<int, int> lookFrom(const Village& v, vec3 from, int k, float nearR, float edge0, float edgeW) {
  const bool near = k & 1;
  const float a = rnd01() * 6.2831853f, d = near ? 4 + rnd01() * nearR : edge0 + rnd01() * edgeW;
  const float ox = near ? from.x : v.x + 0.5f, oz = near ? from.z : v.z + 0.5f;
  return {ifloor(ox + std::cos(a) * d), ifloor(oz + std::sin(a) * d)};
}
// a grown tree out past the houses: its lowest log on the soil, leaves round its crown (not a log in someone's wall)
bool findTree(World& w, const Village& v, vec3 from, ivec3& base) {
  int why[6] = {0, 0, 0, 0, 0, 0};
  struct Rep { int* w; ~Rep() { if (std::getenv("DQ_CHORE_DEBUG")) DQ_LOG("findTree: bld %d top %d leafless %d nolog %d soil %d crown %d", w[0], w[1], w[2], w[3], w[4], w[5]); } } rep{why};
  for (int k = 0; k < 160; k++) {
    auto [x, z] = lookFrom(v, from, k, 44, v.r * 0.5f, v.r * 0.7f + 34);
    if (std::hypot((float)(x - v.x), (float)(z - v.z)) > v.r + 60) continue;   // (x, z: moved onto the trunk below)
    if (inBuilding(v, x, z, 3)) { why[0]++; continue; }
    int y; BlockId id;
    if (!surfaceAt(w, x, z, ifloor(from.y), y, id)) { why[1]++; continue; }
    if (!reg().block(id).has("isLeaves")) { why[2]++; continue; }
    // the trunk under this crown: straight down, or a step or two aside (a look lands anywhere on the leaves)
    int ly = y - 15;
    for (int r = 0; r <= 3 && ly < y - 14; r++) for (int dz = -r; dz <= r && ly < y - 14; dz++) for (int dx = -r; dx <= r; dx++) {
      if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
      int yy = y; while (yy > y - 14 && !isLog(w.getBlock(x + dx, yy, z + dz))) yy--;
      if (isLog(w.getBlock(x + dx, yy, z + dz))) { x += dx; z += dz; ly = yy; break; }
    }
    if (ly < y - 14) { why[3]++; continue; }
    const int top = ly;
    while (isLog(w.getBlock(x, ly - 1, z))) ly--;
    if (!isSoil(w.getBlock(x, ly - 1, z))) { why[4]++; continue; }
    int leaves = 0;
    for (int dy = -1; dy <= 2; dy++) for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) if (reg().block(w.getBlock(x + dx, top + dy, z + dz)).has("isLeaves")) leaves++;
    if (leaves < 6) { why[5]++; continue; }
    base = ivec3(x, ly, z);
    return true;
  }
  return false;
}
// bare stone, or water with a shore, near where they are or round the village (a few random columns a look)
bool findSurface(World& w, const Village& v, vec3 from, bool water, ivec3& spot, ivec3& stand) {
  for (int k = 0; k < 120; k++) {
    auto [x, z] = lookFrom(v, from, k, 40, v.r * 0.4f, v.r * 0.7f + (water ? 40 : 30));
    if (inBuilding(v, x, z, 1)) continue;
    int y; BlockId id;
    if (!surfaceAt(w, x, z, ifloor(from.y), y, id)) continue;
    if (water) {   // open water, or ice with water under it (they fish through a hole)
      static const BlockId ICE = reg().blockId("ice");
      if (!(reg().tLiquid[id] == 1 || (id == ICE && reg().tLiquid[w.getBlock(x, y - 1, z)] == 1))) continue;
    } else if (!stoneLike(id)) continue;
    if (!standBeside(w, x, y + (water ? 0 : 1), z, stand)) continue;
    spot = ivec3(x, y, z);
    return true;
  }
  return false;
}
// fell a tree from its lowest log: every log of it, a sapling in the stump's place
int fell(Level& lv, World& w, ivec3 base, std::string& logKey) {
  const BlockId log = w.getBlock(base.x, base.y, base.z);
  if (!isLog(log)) return 0;
  logKey = reg().block(log).key;
  std::vector<ivec3> q{base}; std::vector<ivec3> got;
  auto seen = [&](ivec3 p) { for (auto& g : got) if (g == p) return true; return false; };
  while (!q.empty() && got.size() < 48) {
    ivec3 p = q.back(); q.pop_back();
    if (seen(p) || w.getBlock(p.x, p.y, p.z) != log) continue;
    got.push_back(p);
    for (int dy = 0; dy <= 1; dy++) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) if (dx || dy || dz) q.push_back(p + ivec3(dx, dy, dz));
  }
  for (auto& p : got) w.setBlock(p.x, p.y, p.z, AIR, 0);
  std::string sap = logKey == "log" ? "sapling" : logKey.substr(0, logKey.size() - 4) + "_sapling";
  const BlockId sid = reg().blockId(sap);
  if (sid && isSoil(w.getBlock(base.x, base.y - 1, base.z))) w.setBlock(base.x, base.y, base.z, sid, 0);
  lv.fx->material("wood", "break", vec3(base) + vec3(0.5f, 1, 0.5f));
  lv.fx->particles("burst", vec3(base) + vec3(0.5f, 1.5f, 0.5f), 12, json{{"block", logKey}, {"speed", 0.6}});
  return (int)got.size();
}
}

OptMove npcChore(Level& lv, Creature& e, NpcMind& M, const Village& v, const VFolk& f, float ph, float dt) {
  World& w = lv.world(e.realm);
  M.choreIdle = false;
  json& S = villageStock(lv, v);
  const std::string& job = f.job;
  const float speed = e.T->speed * 0.45f;
  auto go = [&](vec3 at) -> OptMove { OptMove m = npcGo(lv, e, M, at, dt, speed); return m ? m : OptMove(Move{speed, std::atan2(-(at.x - e.pos.x), -(at.z - e.pos.z))}); };
  auto face = [&](vec3 at) { e.yaw = std::atan2(-(at.x - e.pos.x), -(at.z - e.pos.z)); };
  M.choreT -= dt;

  // ---- the farmer ----
  if (job == "farmer") {
    const VBld* farm = nearestOf(v, "farm", vec3(f.work));
    const VBld* mill = nearestOf(v, "windmill", vec3(f.work));
    if (M.carryN >= 8 || (M.carryN > 0 && ph > 0.25f)) {   // an armful, or the morning nearly done: off with it
      const VBld* to = M.carry == "wheat" && mill ? mill : nearestOf(v, "hall", vec3(f.work));
      const vec3 at = to ? doorOf(*to) : vec3(v.x + 0.5f, (float)v.y + 1, v.z + 0.5f);
      if (!by(e, at, 1.8f)) return go(at);
      stockAdd(S, M.carry, M.carryN);
      if (M.carry == "wheat") npcSay(lv, e, "Wheat for the mill!", 0.35f);
      M.carry.clear(); M.carryN = 0; M.hasCrop = false; holding(e, M, job); M.choreT = 1.5f;
      return Move{0, e.yaw};
    }
    if (!farm) return std::nullopt;
    if (M.choreT > 0 && !M.hasCrop) return std::nullopt;
    if (M.hasCrop && !ripe(w.getBlock(M.crop.x, M.crop.y, M.crop.z), w.getMeta(M.crop.x, M.crop.y, M.crop.z))) M.hasCrop = false;
    if (!M.hasCrop) {
      float bd = 1e9f;
      for (int z = std::min(farm->z0, farm->z1) + 1; z < std::max(farm->z0, farm->z1); z++) for (int x = std::min(farm->x0, farm->x1) + 1; x < std::max(farm->x0, farm->x1); x++)
        for (int y = farm->y + 1; y <= farm->y + 2; y++) {
          if (!w.isLoaded(x, y, z)) continue;
          const BlockId id = w.getBlock(x, y, z);
          if (!id || !ripe(id, w.getMeta(x, y, z))) continue;
          const float d = std::hypot(x + 0.5f - e.pos.x, z + 0.5f - e.pos.z);
          if (d < bd) { bd = d; M.crop = ivec3(x, y, z); M.hasCrop = true; }
        }
      if (!M.hasCrop) { M.choreT = 6; return std::nullopt; }   // nothing ripe: tend the rows at the gate
    }
    const vec3 at = vec3(M.crop) + vec3(0.5f, 0, 0.5f);
    if (!by(e, at, 1.3f)) return go(at);
    const BlockId id = w.getBlock(M.crop.x, M.crop.y, M.crop.z);
    const BlockDef& d = reg().block(id);
    const std::string item = cropItem(d);
    M.hasCrop = false;
    if (item.empty()) return Move{0, e.yaw};
    if (!M.carry.empty() && M.carry != item && M.carryN > 0) { stockAdd(S, M.carry, M.carryN); M.carryN = 0; }
    w.setBlock(M.crop.x, M.crop.y, M.crop.z, id, 0);   // reaped and sown again
    lv.fx->particles("burst", at + vec3(0, 0.4f, 0), 5, json{{"block", d.key}, {"speed", 0.4}});
    lv.fx->material("grass", "dig", at);
    M.carry = item; M.carryN += item == "wheat" ? 1 : 2;
    holding(e, M, job); e.swingT = 0.3f; face(at); M.choreT = 0.7f;
    return Move{0, e.yaw};
  }

  // ---- the miller ----
  if (job == "miller") {
    if (M.choreT > 0) return std::nullopt;
    M.choreT = 4;
    const VBld* mill = nearestOf(v, "windmill", vec3(f.work));
    ivec3 ms;
    const bool found = mill && millstoneIn(w, *mill, ms);
    if (std::getenv("DQ_CHORE_DEBUG")) DQ_LOG("chore miller: mill %d (%d,%d..%d,%d y%d) stones %d at %d %d %d, wheat %d", mill ? 1 : 0, mill ? mill->x0 : 0, mill ? mill->z0 : 0, mill ? mill->x1 : 0, mill ? mill->z1 : 0, mill ? mill->y : 0, (int)found, ms.x, ms.y, ms.z, stockOf(S, "wheat"));
    if (found) {
      json* be = w.blockEntity(ms.x, ms.y, ms.z, true);
      if (!be) return std::nullopt;
      if (!be->contains("type")) (*be)["type"] = "machine";
      json& it = (*be)["items"];
      if (!it.is_array()) it = json::array({nullptr, nullptr});
      while (it.size() < 2) it.push_back(nullptr);
      ItemStack in = ItemStack::fromJson(it[0]), out = ItemStack::fromJson(it[1]);
      bool changed = false;
      if (!out.empty() && out.id == "flour") { stockAdd(S, "flour", out.n); it[1] = nullptr; changed = true; }
      const int wheat = stockOf(S, "wheat");
      if (wheat > 0 && (in.empty() || in.id == "wheat") && (in.empty() ? 0 : in.n) < 32) {
        const int put = std::min(wheat, 32 - (in.empty() ? 0 : in.n));
        if (in.empty()) in = ItemStack("wheat", put); else in.n += put;
        it[0] = in.toJson(); stockAdd(S, "wheat", -put); changed = true;
      }
      if (changed) { w.blockEntityChanged(ms.x, ms.y, ms.z); e.swingT = 0.3f; lv.fx->sound("click", e.pos + vec3(0, 1, 0), 10); }
    } else if (stockOf(S, "wheat") > 0) {   // no stones turning here (an older windmill): a quern, by hand, slowly
      M.choreT = 12; stockAdd(S, "wheat", -1); stockAdd(S, "flour", 1);
      e.swingT = 0.3f; lv.fx->sound("grind", e.pos + vec3(0, 1, 0), 12);
    }
    return std::nullopt;
  }

  // ---- the baker ----
  if (job == "baker") {
    const VBld* mill = nearestOf(v, "windmill", vec3(f.work));
    if (M.carryN <= 0 && stockOf(S, "flour") > 0 && ph < 0.2f) {
      const vec3 at = mill ? doorOf(*mill) : vec3(f.work) + vec3(0.5f, 0, 0.5f);
      if (!by(e, at, 1.8f)) return go(at);
      const int take = std::min(8, stockOf(S, "flour"));
      stockAdd(S, "flour", -take); M.carry = "flour"; M.carryN = take; holding(e, M, job);
      return Move{0, e.yaw};
    }
    if (M.carryN > 0 && M.carry == "flour") {
      const vec3 at = vec3(f.work) + vec3(0.5f, 0, 0.5f);
      if (!by(e, at, 1.2f)) return go(at);
      if (M.choreT <= 0) {
        M.choreT = 6; M.carryN--; stockAdd(S, "bread", 1);
        lv.fx->particles("smoke", at + vec3(0, 1.6f, 0), 6); lv.fx->sound("fire", at + vec3(0, 1, 0), 10); e.swingT = 0.3f;
        if (M.carryN <= 0) { M.carry.clear(); M.carryN = 0; holding(e, M, job); npcSay(lv, e, "Fresh bread!", 0.4f); }
      }
      return Move{0, e.yaw};
    }
    return std::nullopt;
  }

  // ---- the builder: what an explosion took, put back as it was ----
  if (job == "builder") {
    json& D = villageRec(lv, v.id)["damage"];
    if (std::getenv("DQ_CHORE_DEBUG") && rnd01() < dt * 0.3f) DQ_LOG("chore builder: %d to mend, at %.1f %.1f %.1f jobT %.1f choreT %.1f", D.is_array() ? (int)D.size() : -1, e.pos.x, e.pos.y, e.pos.z, M.jobT, M.choreT);
    if (!D.is_array() || D.empty()) { M.choreIdle = true; return std::nullopt; }   // nothing to mend: about the village
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < (int)D.size(); i++) {
      const json& d = D[i];
      const float dd = std::hypot(d[0].get<int>() + 0.5f - e.pos.x, d[2].get<int>() + 0.5f - e.pos.z) + std::fabs(d[1].get<int>() - e.pos.y) * 0.5f;
      if (dd < bd) { bd = dd; best = i; }
    }
    const json d = D[best];
    const ivec3 p(d[0].get<int>(), d[1].get<int>(), d[2].get<int>());
    if (!w.isLoaded(p.x, p.y, p.z)) return std::nullopt;
    const vec3 at = vec3(p) + vec3(0.5f, 0, 0.5f);
    // within a reach of it (like yours: six blocks): from the crater's rim they can fill its bottom
    if (glm::distance(e.pos + vec3(0, 1.5f, 0), at + vec3(0, 0.5f, 0)) > 6.f) {
      M.jobT += dt;
      if (M.jobT > 20) { M.jobT = 0; json moved = D[best]; D.erase(D.begin() + best); D.push_back(moved); return std::nullopt; }   // (can't get there: the next one)
      ivec3 st; if (standBeside(w, p.x, p.y, p.z, st)) return go(vec3(st) + vec3(0.5f, 0, 0.5f));
      return go(at);
    }
    M.jobT = 0;
    face(at);
    if (M.choreT > 0) return Move{0, e.yaw};
    M.choreT = 0.9f;
    const BlockId cur = w.getBlock(p.x, p.y, p.z), want = reg().blockId(d[3].get<std::string>());
    bool body = false;   // nobody (but the builder: they step up out of it) standing in it
    // (someone: a player or a creature. The things the blast scattered lie in the crater: counted, every spot of it was
    //  "taken", went to the back of the list, and the builder stood there going round them for ever)
    for (auto& o : lv.entities()) if (o.get() != &e && !o->removed && o->realm == e.realm && (o->isPlayer() || dynamic_cast<const Creature*>(o.get())) &&
                                      std::fabs(o->pos.x - at.x) < 0.8f && std::fabs(o->pos.z - at.z) < 0.8f && o->pos.y > p.y - 1.8f && o->pos.y < p.y + 1) body = true;
    if (body) { json moved = D[best]; D.erase(D.begin() + best); D.push_back(moved); return Move{0, e.yaw}; }   // (someone's there: the next one first)
    if (std::fabs(e.pos.x - at.x) < 0.8f && std::fabs(e.pos.z - at.z) < 0.8f && e.pos.y > p.y - 1.8f && e.pos.y < p.y + 1) e.pos.y = (float)p.y + 1.01f;   // in the way themselves: up onto it
    if (want && (cur == AIR || reg().tReplaceable[cur] || reg().block(cur).has("fire"))) {
      w.setBlock(p.x, p.y, p.z, want, (u8)d[4].get<int>());
      if (d.size() >= 8) {   // as it was: its chiselling, its rug, its goods (villageDamage)
        if (const int cv = d[5].is_number() ? d[5].get<int>() : 0) w.setCarve(p.x, p.y, p.z, (u8)cv);
        if (const int co = d[6].is_number() ? d[6].get<int>() : 0) w.setCoat(p.x, p.y, p.z, (u8)co, SetFlags::Urgent);
        if (d[7].is_object()) w.setBlockEntity(p.x, p.y, p.z, d[7]);
      }
      lv.fx->material(reg().block(want).str("sound").empty() ? std::string("stone") : reg().block(want).str("sound"), "place", at);
      lv.fx->particles("burst", at + vec3(0, 0.5f, 0), 4, json{{"block", d[3].get<std::string>()}, {"speed", 0.3}});
      e.swingT = 0.3f;
    }
    D.erase(D.begin() + best);
    if (D.empty()) npcSay(lv, e, "Good as new.", 0.4f);
    return Move{0, e.yaw};
  }

  // ---- the gatherers: walk out to the place, work it a while, bring something back to the stores ----
  auto gather = [&](float secs, auto find, auto produce) -> OptMove {
    if (std::getenv("DQ_CHORE_DEBUG") && rnd01() < dt * 0.3f) DQ_LOG("chore %s: has %d choreT %.1f jobT %.1f at %.1f %.1f %.1f -> %.1f %.1f %.1f", job.c_str(), (int)M.hasCrop, M.choreT, M.jobT, e.pos.x, e.pos.y, e.pos.z, M.choreAt.x, M.choreAt.y, M.choreAt.z);
    if (!M.hasCrop) {
      if (M.choreT > 0) { M.choreIdle = true; return std::nullopt; }
      ivec3 spot, stand;
      if (!find(spot, stand)) { if (std::getenv("DQ_CHORE_DEBUG")) DQ_LOG("chore %s: nothing found", job.c_str()); M.choreT = 10; M.choreIdle = true; return std::nullopt; }
      M.crop = spot; M.hasCrop = true; M.jobT = 0;
      M.choreAt = vec3(stand) + vec3(0.5f, 0, 0.5f);
    }
    const vec3 st = M.choreAt;
    if (!by(e, st, 1.5f)) {
      M.jobT += dt * 0.25f;
      if (M.jobT > 25) { M.hasCrop = false; M.choreT = 4; return std::nullopt; }   // (can't get there: another spot)
      // stuck a long while and nobody about to see: they know the way (as the village's own walk home does)
      if (M.stuckGo > 10 && !nearestPawn(lv, e.realm, e.pos, 24)) { e.pos = st + vec3(0, 0.01f, 0); M.stuckGo = 0; M.hasPath = false; return Move{0, e.yaw}; }
      // a walled town: out (or in) by the nearest gate, not straight at the wall
      if (v.hasWall && !v.gates.empty()) {
        auto inside = [&](vec3 p) { return p.x > v.wx0 && p.x < v.wx1 + 1 && p.z > v.wz0 && p.z < v.wz1 + 1; };
        const bool inE = inside(e.pos), inT = inside(st);
        if (inE != inT) {
          const VGate* g = nullptr; float gd = 1e9f;
          for (auto& G : v.gates) { const float d = glm::distance(vec2(e.pos.x, e.pos.z), vec2(G.center.x, G.center.z)) + glm::distance(vec2(G.center.x, G.center.z), vec2(st.x, st.z)); if (d < gd) { gd = d; g = &G; } }
          const vec3 gin = g->center + vec3(g->inX * 3.f, 0, g->inZ * 3.f), gout = g->center - vec3(g->inX * 3.f, 0, g->inZ * 3.f);
          const vec3 nearSide = inE ? gin : gout, farSide = inE ? gout : gin;
          if (!by(e, nearSide, 1.6f) && glm::distance(vec2(e.pos.x, e.pos.z), vec2(g->center.x, g->center.z)) > 2.2f) return go(nearSide);
          return go(farSide);
        }
      }
      // far off: a leg at a time, each within the paths' reach
      const vec2 dv(st.x - e.pos.x, st.z - e.pos.z);
      const float dl = glm::length(dv);
      if (dl > 30) {
        const vec2 m = vec2(e.pos.x, e.pos.z) + dv / dl * 26.f;
        int y; BlockId id;
        if (topAt(w, v, ifloor(m.x), ifloor(m.y), y, id) && !reg().tLiquid[id]) return go(vec3(ifloor(m.x) + 0.5f, (float)y + 1, ifloor(m.y) + 0.5f));
      }
      return go(st);
    }
    face(vec3(M.crop) + vec3(0.5f));
    M.jobT += dt;
    if ((int)(M.jobT * 1.6f) != (int)((M.jobT - dt) * 1.6f)) e.swingT = 0.3f;
    if (M.jobT >= secs) { produce(); M.hasCrop = false; M.choreT = 3 + rnd01() * 4; }
    return Move{0, e.yaw};
  };
  if (job == "lumberjack") {
    return gather(7.f, [&](ivec3& spot, ivec3& stand) { return findTree(w, v, e.pos, spot) && standBeside(w, spot.x, spot.y, spot.z, stand); },
      [&]() { std::string k; const int n = fell(lv, w, M.crop, k); if (n > 0) { stockAdd(S, k, n); npcSay(lv, e, "Timber!", 0.5f); } });
  }
  if (job == "miner") {
    // the town mine (gen/town_mine.h, built by gen/structures_townbld.cpp bMine): down the drift and out along one of its
    // galleries (a different one each day) to the face, a block of it at a time into the stores; a face cut right out is
    // made good with fresh stone and ore. A gallery whose face has the land close over it is left be (he'd break out).
    if (const VBld* mine = nearestOf(v, "mine", vec3(f.work))) {
      using namespace townmine;
      auto at = [&](int lx, int ly, int lz) {
        int a = lx, c = lz;
        switch (mine->r & 3) { case 1: a = -lz; c = lx; break; case 2: a = -lx; c = -lz; break; case 3: a = lz; c = -lx; break; default: break; }
        return ivec3(mine->ox + a, mine->y + ly, mine->oz + c);
      };
      auto rock = [&](BlockId id) { if (!id || !reg().tSolid[id]) return false; const std::string& t = reg().block(id).tool; return t == "pickaxe" || t == "shovel"; };
      auto cut = [&]() {   // a block of the face into the stores
        const ivec3 c = M.crop; const BlockId id = w.getBlock(c.x, c.y, c.z);
        const std::string k = reg().block(id).key;
        const std::string item = k == "coal_ore" ? "coal" : k == "iron_ore" ? "iron_ore" : k == "copper_ore" ? "copper_ore" : "cobblestone";
        stockAdd(S, item, item == "coal" ? 2 : 1);
        if (item != "cobblestone") stockAdd(S, "cobblestone", 1);
        if (item == "iron_ore") npcSay(lv, e, "Iron!", 0.4f);
        lv.fx->material("stone", "break", vec3(c) + vec3(0.5f));
        lv.fx->particles("burst", vec3(c) + vec3(0.5f), 10, json{{"block", k}, {"speed", 0.5}});
        w.setBlock(c.x, c.y, c.z, AIR, 0);
      };
      if (mine->planV < 11)   // the old mine (worlds made before the deep one): its one face at the drift's end
        return gather(5.f, [&](ivec3& spot, ivec3& stand) {
            for (int pass = 0; pass < 2; pass++) {
              for (int ly = -4; ly >= -6; ly--) for (int lx = 2; lx <= 4; lx++) {
                const ivec3 c = at(lx, ly, 11);
                if (!w.isLoaded(c.x, c.y, c.z)) return false;
                const BlockId id = w.getBlock(c.x, c.y, c.z);
                if (id && reg().tSolid[id]) { spot = c; stand = at(lx, -6, 10); return true; }
              }
              static const char* ORE[7] = {"stone", "coal_ore", "stone", "iron_ore", "coal_ore", "stone", "copper_ore"};
              for (int ly = -4; ly >= -6; ly--) for (int lx = 2; lx <= 4; lx++) {
                const ivec3 c = at(lx, ly, 11); const BlockId o = reg().blockId(ORE[(int)(rnd01() * 7) % 7]);
                w.setBlock(c.x, c.y, c.z, o ? o : reg().blockId("stone"), 0);
              }
            }
            return false;
          }, cut);
      const int day = (int)(npcDay(lv) % 3);
      return gather(5.f, [&](ivec3& spot, ivec3& stand) {
          for (int k = 0; k < 3; k++) {
            const int g = (day + k) % 3; const Gallery& G = GAL[g];
            int face = -1;   // the first step along it with rock in its 3x3 (its posts, caps and rail aren't rock)
            for (int st = 1; st <= G.len && face < 0; st++)
              for (int side = -1; side <= 1 && face < 0; side++) for (int ly = FLOOR + 1; ly <= FLOOR + 3; ly++) {
                int lx, lz; galleryCell(g, st, side, lx, lz); const ivec3 c = at(lx, ly, lz);
                if (!w.isLoaded(c.x, c.y, c.z)) return false;
                if (rock(w.getBlock(c.x, c.y, c.z))) { face = st; break; }
              }
            if (face < 0) face = G.len;
            { int lx, lz; galleryCell(g, face - 1, 0, lx, lz); const ivec3 s0 = at(lx, FLOOR + 1, lz);
              if (reg().tSolid[w.getBlock(s0.x, s0.y, s0.z)] || reg().tSolid[w.getBlock(s0.x, s0.y + 1, s0.z)]) continue; }   // (not dug to there)
            bool shallow = false;
            { int lx, lz; galleryCell(g, face, 0, lx, lz);
              for (int ly = FLOOR + 4; ly <= FLOOR + 9 && !shallow; ly++) { const ivec3 c = at(lx, ly, lz); if (!reg().tSolid[w.getBlock(c.x, c.y, c.z)] && !reg().tLiquid[w.getBlock(c.x, c.y, c.z)]) shallow = true; } }
            if (shallow) continue;
            for (int pass = 0; pass < 2; pass++) {
              for (int ly = FLOOR + 3; ly >= FLOOR + 1; ly--) for (int side = -1; side <= 1; side++) {
                int lx, lz; galleryCell(g, face, side, lx, lz); const ivec3 c = at(lx, ly, lz);
                if (rock(w.getBlock(c.x, c.y, c.z))) { int sx, sz; galleryCell(g, face - 1, 0, sx, sz); spot = c; stand = at(sx, FLOOR + 1, sz); return true; }
              }
              static const char* ORE[7] = {"stone", "coal_ore", "stone", "iron_ore", "coal_ore", "stone", "copper_ore"};
              for (int ly = FLOOR + 1; ly <= FLOOR + 3; ly++) for (int side = -1; side <= 1; side++) {
                int lx, lz; galleryCell(g, face, side, lx, lz); const ivec3 c = at(lx, ly, lz); const BlockId o = reg().blockId(ORE[(int)(rnd01() * 7) % 7]);
                w.setBlock(c.x, c.y, c.z, o ? o : reg().blockId("stone"), 0);
              }
            }
          }
          return false;
        }, cut);
    }
    return gather(9.f, [&](ivec3& spot, ivec3& stand) { return findSurface(w, v, e.pos, false, spot, stand); },
      [&]() {
        stockAdd(S, "cobblestone", 3);
        const float r = rnd01();
        if (r < 0.35f) stockAdd(S, "coal", 1 + (rnd01() < 0.4f ? 1 : 0));
        else if (r < 0.52f) { stockAdd(S, "iron_ore", 1); npcSay(lv, e, "Iron!", 0.4f); }
        else if (r < 0.64f) stockAdd(S, "copper_ore", 1);
        else if (r < 0.74f) stockAdd(S, "flint", 1);
        lv.fx->material("stone", "break", vec3(M.crop) + vec3(0.5f, 1, 0.5f));
        lv.fx->particles("burst", vec3(M.crop) + vec3(0.5f, 1.1f, 0.5f), 8, json{{"block", reg().block(w.getBlock(M.crop.x, M.crop.y, M.crop.z)).key}, {"speed", 0.5}});
      });
  }
  if (job == "fisherman") {
    return gather(12.f, [&](ivec3& spot, ivec3& stand) { return findSurface(w, v, e.pos, true, spot, stand); },
      [&]() {
        stockAdd(S, rnd01() < 0.65f ? "cod" : "salmon", 1);
        lv.fx->particles("splash", vec3(M.crop) + vec3(0.5f, 1, 0.5f), 6);
        lv.fx->sound("splash", vec3(M.crop) + vec3(0.5f, 1, 0.5f), 12);
      });
  }
  if (job == "rancher" || job == "shepherd") {
    const bool sheep = job == "shepherd";
    const VBld* pen = sheep ? nearestOf(v, "pen", vec3(f.work), "sheep") : nearestOf(v, "pen", vec3(f.work), "sheep", true);
    if (!pen) return std::nullopt;
    return gather(14.f, [&](ivec3& spot, ivec3& stand) { spot = pen->hasMid ? pen->mid : pen->door; stand = spot; return true; },
      [&]() {
        if (sheep) { stockAdd(S, "wool", 2); lv.fx->sound("shear", e.pos, 12); }
        else if (pen->animals == "chicken") { stockAdd(S, "egg", 2); stockAdd(S, "feather", 1); }
        else if (pen->animals == "cow") stockAdd(S, "beef", 1);
        else if (pen->animals == "pig") stockAdd(S, "porkchop", 1);
        lv.fx->particles("heart", e.pos + vec3(0, 1.8f, 0), 2);
      });
  }

  // ---- the crafts, at their own workplace: the stores' raw stuff into goods ----
  struct Craft { const char* job; std::vector<std::pair<const char*, int>> in; std::pair<const char*, int> out; float every; const char* sound; };
  static const std::vector<Craft> CRAFTS = {
    {"mason", {{"cobblestone", 2}}, {"stone_bricks", 2}, 8, "anvil"},
    {"armorer", {{"iron_ore", 1}, {"coal", 1}}, {"iron_ingot", 1}, 12, "anvil"},
    {"weaponsmith", {{"iron_ore", 1}, {"coal", 1}}, {"iron_ingot", 1}, 12, "anvil"},
    {"toolsmith", {{"iron_ore", 1}, {"coal", 1}}, {"iron_ingot", 1}, 12, "anvil"},
    {"butcher", {{"beef", 1}}, {"cooked_beef", 1}, 10, "fire"},
    {"butcher", {{"porkchop", 1}}, {"cooked_porkchop", 1}, 10, "fire"},
    {"butcher", {{"chicken", 1}}, {"cooked_chicken", 1}, 10, "fire"},
    {"butcher", {{"mutton", 1}}, {"cooked_mutton", 1}, 10, "fire"},
    {"fletcher", {{"stick", 1}, {"flint", 1}, {"feather", 1}}, {"arrow", 4}, 8, "bow"},
    {"fletcher", {{"*log", 1}}, {"stick", 4}, 6, "wood"},
    {"innkeeper", {{"carrot", 1}, {"potato", 1}}, {"vegetable_stew", 1}, 12, "fire"},
    {"innkeeper", {{"beetroot", 2}}, {"vegetable_stew", 1}, 12, "fire"},
  };
  bool any = false; for (auto& c : CRAFTS) if (job == c.job) any = true;
  if (!any || M.choreT > 0) return std::nullopt;
  if (!by(e, vec3(f.work) + vec3(0.5f, 0, 0.5f), 2.2f)) return std::nullopt;   // (on the way to the workplace: the caller walks them)
  for (auto& c : CRAFTS) {
    if (job != c.job) continue;
    bool ok = true;
    std::vector<std::pair<std::string, int>> take;
    for (auto& [k, n] : c.in) { std::string key = std::string(k) == "*log" ? anyLog(S) : std::string(k); if (key.empty() || stockOf(S, key) < n) { ok = false; break; } take.push_back({key, n}); }
    if (!ok || !reg().item(c.out.first)) continue;
    for (auto& [k, n] : take) stockAdd(S, k, -n);
    stockAdd(S, c.out.first, c.out.second);
    e.swingT = 0.3f;
    lv.fx->sound(c.sound, e.pos + vec3(0, 1, 0), 12);
    lv.fx->particles(std::string(c.sound) == "fire" ? "smoke" : "burst", vec3(f.work) + vec3(0.5f, 1.4f, 0.5f), 4, json{{"item", c.out.first}, {"speed", 0.3}});
    M.choreT = c.every;
    return std::nullopt;
  }
  M.choreT = 5;   // nothing in the stores to work: wait a bit
  return std::nullopt;
}

} // namespace dq
