// sim/bta_more.cpp — the rest of BTA's blocks and items (our own designs; data from tools/port/bta_more.py):
// - the Emberglass conduit: a run carries a level that drops by one each block. Dust feeds it one below its own level.
//   A redstone block, a redstone pumpkin, or a matcher that is on feeds 15. A powered solid block feeds 15 too.
// - the matcher: on while the two blocks on its axis are the same kind. It powers the four sides that are not on that axis.
// - the gold mesh: only the item set on it (same id and damage) falls through, and only then does it pull items in
//   through still water. An iron mesh pulls any item. (sim/physics.cpp reads "filter" and "filterD".)
// - paintings take a gold or an iron trim (use an ingot on one; shears take it off), drawn by render/painting_layer.cpp.
// - the summoning wand (creative): use it to call up its creature, sneak and use to pick the next one.
// - the paintbrush dyes a sheep's wool (with a dye in the other hand).
#include "sim/creature.h"
#include "sim/player.h"
#include "sim/controller.h"
#include "sim/redstone.h"
#include "sim/gameplay.h"
#include "sim/physics.h"
#include "sim/steam.h"
#include "sim/mob_combat.h"
#include "sim/level.h"
#include "core/registry.h"
#include <cmath>
#include <unordered_set>

namespace dq {

namespace {
// a filter on a block entity: "filter" = an item key ("" = cleared)
bool setFilter(PlayerController& pc, const RayHit& hit, ItemStack* held, const char* what) {
  World& w = pc.lv.world(pc.p.realm);
  json* e = w.blockEntity(hit.block.x, hit.block.y, hit.block.z, true);
  if (!e) return false;
  if (!e->is_object()) *e = json::object();
  (*e)["type"] = what;
  const bool clear = !held || held->empty();
  if (clear) { e->erase("filter"); e->erase("filterD"); }
  else {
    (*e)["filter"] = held->id;
    (*e)["filterD"] = held->d;
    if (!pc.p.creative()) { if (--held->n <= 0) held->clear(); }
  }
  w.blockEntityChanged(hit.block.x, hit.block.y, hit.block.z);
  const ItemDef* d = clear ? nullptr : reg().item(held->id);
  std::string name = d ? d->name : clear ? "" : held->id;
  toast(pc.lv, pc.p, clear ? "The gold mesh lets nothing through now" : "Only " + name + " falls through", "info");
  pc.lv.fx->material("metal", "place", vec3(hit.block) + vec3(0.5f));
  pc.swingArm();
  return true;
}

const std::vector<std::string>& wandMobs() {
  static std::vector<std::string> v;
  if (!v.empty() || !reg().mobs.is_object()) return v;
  static const char* SKIP[] = {"herobrine", "end_crystal", "wither_storm"};   // the story's figure and its climax: never by wand
  for (auto& [k, m] : reg().mobs.items()) {
    bool skip = false; for (const char* s : SKIP) if (k == s) skip = true;
    if (!skip && reg().item(k + "_spawn_egg")) v.push_back(k);
  }
  return v;
}
} // namespace

// the steam vents: whoever stands over one while it breathes is scalded and lifted a little
class SteamVents : public LevelSystem {
public:
  const char* name() const override { return "steam_vents"; }
  double acc = 0;
  std::unordered_map<u32, double> lastHit;
  void tick(Level& lv, double dt) override {
    if (lv.isRemote() || (acc += dt) < 0.25) return;
    acc = 0;
    static const BlockId vent = reg().blockId("steam_vent");
    if (!vent) return;
    const double t = lv.info.time;
    for (auto& up : lv.entities()) {
      Entity& e = *up;
      if (e.removed || e.realm != REALM_MAIN || e.hp <= 0) continue;
      const bool player = e.isPlayer();
      if (!player && !dynamic_cast<Creature*>(&e)) continue;
      const int ex = ifloor(e.pos.x), ey = ifloor(e.pos.y), ez = ifloor(e.pos.z);
      if (dimOfY(ey) != DIM_NETHER) continue;
      World& w = lv.world(e.realm);
      for (int k = 1; k <= 3; k++) {
        if (w.getBlock(ex, ey - k, ez) != vent) { if (reg().tSolid[w.getBlock(ex, ey - k, ez)]) break; continue; }
        if (!steamGust(t, ex, ey - k, ez)) break;
        double& lh = lastHit[e.id];
        if (t - lh > 0.75 || t < lh) {
          lh = t;
          if (!player || !static_cast<Player&>(e).creative()) { DamageSrc s; damageEntity(lv, e, 1, nullptr, "burn", s); }
        }
        e.vel.y = std::max(e.vel.y, 4.5f);   // the plume lifts you
        break;
      }
    }
  }
};

void registerBtaMore() {
  static bool done = false;
  if (done) return;
  done = true;
  registerLevelSystem([](Level&) -> std::shared_ptr<LevelSystem> { return std::make_shared<SteamVents>(); });
  // the gold mesh and the matcher take a filter; paintings take a trim
  hooks().useBlock.push_back([](PlayerController& pc, const RayHit& hit, ItemStack* held, const ItemDef*) {
    const BlockDef& b = reg().block(hit.id);
    if (pc.lv.isRemote()) return (b.has("meshFilter") || (b.has("painting") && held && !held->empty())) && !pc.p.sneaking ? useOnHost(pc, "block", &hit) : false;
    if (b.has("meshFilter") && !pc.p.sneaking) return setFilter(pc, hit, held, "mesh");
    if (b.has("painting") && held && !held->empty() && (held->id == "gold_ingot" || held->id == "iron_ingot" || held->id == "shears")) {
      World& w = pc.lv.world(pc.p.realm);
      json* e = w.blockEntity(hit.block.x, hit.block.y, hit.block.z, true);
      if (!e) return false;
      if (!e->is_object()) *e = {{"type", "painting"}, {"n", 0}};
      if (held->id == "shears") { if (!e->contains("trim")) return false; e->erase("trim"); }
      else {
        std::string t = held->id == "gold_ingot" ? "gold" : "iron";
        if (e->value("trim", std::string()) == t) return false;
        (*e)["trim"] = t;
        if (!pc.p.creative()) { if (--held->n <= 0) held->clear(); }
      }
      w.blockEntityChanged(hit.block.x, hit.block.y, hit.block.z);
      pc.lv.fx->material("metal", "place", vec3(hit.block) + vec3(0.5f));
      pc.swingArm();
      return true;
    }
    return false;
  });
  // the summoning wand
  hooks().useItem.push_back([](PlayerController& pc, ItemStack& held, const ItemDef& def, bool repeat) {
    if (repeat || !def.has("summonWand")) return false;
    if (pc.lv.isRemote()) return useOnHost(pc, "item");
    const auto& L = wandMobs();
    if (L.empty()) return true;
    if (!held.tag.is_object()) held.tag = json::object();
    std::string cur = held.tag.value("mob", std::string("pig"));
    if (pc.p.sneaking) {
      size_t i = 0; while (i < L.size() && L[i] != cur) i++;
      cur = L[(i + 1) % L.size()];
      held.tag["mob"] = cur;
      const json& M = reg().mobs[cur];
      toast(pc.lv, pc.p, "The wand calls up: " + M.value("label", cur), "info");
      return true;
    }
    vec3 at;
    if (pc.target.hit) at = vec3(pc.target.block) + vec3(0.5f, 0, 0.5f) + vec3(pc.target.normal) * (pc.target.normal.y == 0 ? 1.f : 0.f) + vec3(0, pc.target.normal.y > 0 ? 1.f : 0.f, 0);
    else { vec3 l = lookVector(pc.p.yaw, pc.p.pitch); at = pc.p.pos + vec3(l.x * 3, 0, l.z * 3); }
    if (spawnCreature(pc.lv, pc.p.realm, cur, at)) { pc.lv.fx->particles("end_mote", at + vec3(0, 0.6f, 0), 12); pc.lv.fx->sound("ender_tp", at, 16); }
    pc.swingArm();
    return true;
  });
  // the paintbrush on a sheep: a dye in the other hand colours its wool
  hooks().useEntity.insert(hooks().useEntity.begin(), [](PlayerController& pc, Entity& e, ItemStack* held, const ItemDef* def) {
    if (!held || !def || !def->has("paintbrush") || pc.lv.isRemote() || e.type != "sheep") return false;
    auto* c = dynamic_cast<Creature*>(&e);
    if (!c) return false;
    ItemStack* dye = pc.p.offhand.empty() || pc.p.offhand[0].empty() ? nullptr : &pc.p.offhand[0];
    const ItemDef* dd = dye ? dye->def() : nullptr;
    std::string col = dd ? dd->str("dye") : "";
    if (col.empty()) { toast(pc.lv, pc.p, "Hold a dye in your other hand to paint with", "info"); return true; }
    if (c->color == col) return true;
    c->color = col;
    pc.lv.fx->particles("dust", c->pos + vec3(0, 0.8f, 0), 8);
    if (!pc.p.creative()) {
      if (--dye->n <= 0) dye->clear();
      held->d++; if (def->hasTool && held->d >= def->tool.durability) held->clear();
    }
    pc.swingArm();
    return true;
  });
}

using rs::Tables; using rs::T; using rs::CONDUIT; using rs::WIRE; using rs::BLOCK; using rs::MATCHER;

// ---- the Emberglass conduit: each block keeps the best feed, then loses one level per step along the run
void Redstone::updateConduit(int x, int y, int z) {
  const Tables& t = T();
  std::vector<ivec3> run, todo{ivec3(x, y, z)};
  std::unordered_set<u64> seen{rs::posKey(x, y, z)};
  while (!todo.empty() && run.size() < 512) {
    ivec3 c = todo.back(); todo.pop_back();
    if (t.role[w.getBlock(c.x, c.y, c.z)] != CONDUIT) continue;
    run.push_back(c);
    for (int d = 0; d < 6; d++) {
      int nx = c.x + rs::D6X[d], ny = c.y + rs::D6Y[d], nz = c.z + rs::D6Z[d];
      if (t.role[w.getBlock(nx, ny, nz)] != CONDUIT) continue;
      if (seen.insert(rs::posKey(nx, ny, nz)).second) todo.push_back(ivec3(nx, ny, nz));
    }
  }
  if (run.empty()) return;
  const int n = (int)run.size();
  std::unordered_map<u64, int> at;
  std::vector<int> ext(n, 0), pow(n, 0);
  for (int i = 0; i < n; i++) at[rs::posKey(run[i].x, run[i].y, run[i].z)] = i;
  for (int i = 0; i < n; i++) {
    int best = 0;
    const ivec3& c = run[i];
    for (int d = 0; d < 6; d++) {
      int nx = c.x + rs::D6X[d], ny = c.y + rs::D6Y[d], nz = c.z + rs::D6Z[d];
      BlockId id = w.getBlock(nx, ny, nz);
      u8 r = t.role[id];
      if (r == CONDUIT || (t.oreLit && id == t.oreLit)) continue;
      int m = w.getMeta(nx, ny, nz);
      if (r == WIRE) best = std::max(best, std::max(0, (m & 15) - 1));
      else if (r == BLOCK) best = 15;
      else if (r == MATCHER && (m & 8)) best = 15;
      else if (r && emit(nx, ny, nz, id, m, rs::OPP6[d]) > 0) best = 15;
      else if (t.cond[id] && blockPower(nx, ny, nz, true) > 0) best = 15;
    }
    ext[i] = pow[i] = best;
  }
  for (int pass = 0; pass < 15; pass++) {
    std::vector<int> nxt = ext;
    for (int i = 0; i < n; i++) {
      int p = ext[i];
      const ivec3& c = run[i];
      for (int d = 0; d < 6; d++) {
        auto it = at.find(rs::posKey(c.x + rs::D6X[d], c.y + rs::D6Y[d], c.z + rs::D6Z[d]));
        if (it == at.end()) continue;
        p = std::max(p, pow[it->second] - 1);
      }
      nxt[i] = std::max(0, p);
    }
    pow.swap(nxt);
  }
  for (int i = 0; i < n; i++) {
    const ivec3& c = run[i];
    BlockId id = w.getBlock(c.x, c.y, c.z);
    int want = pow[i];
    bool lit = t.conduitLit[id] != 0;
    if (want <= 0) {
      if (lit && t.conduitFlip[id]) w.setBlock(c.x, c.y, c.z, t.conduitFlip[id], 0);
    } else {
      BlockId litId = lit ? id : t.conduitFlip[id];
      if (!litId) continue;
      int nm = want & 15;
      if (id != litId || (w.getMeta(c.x, c.y, c.z) & 15) != nm) w.setBlock(c.x, c.y, c.z, litId, nm);
    }
  }
}

// Same kind: one id, and full cubes ignore meta. Different ids match when they share a shape and a meta (two stairs).
static bool matcherSame(BlockId a, int ma, BlockId b, int mb) {
  if (!a && !b) return true;
  const Registry& R = reg();
  if (a == b) return R.tShape[a] == 0 || ma == mb;
  int sa = a ? R.tShape[a] : 0, sb = b ? R.tShape[b] : 0;
  return sa && sa == sb && ma == mb;
}

// ---- the matcher: on while the blocks on either end of its axis agree
void Redstone::matcherTick(int x, int y, int z, BlockId id, int m) {
  int axis = rs::matcherAxis(m);
  int d = axis == 0 ? 5 : axis == 1 ? 1 : 3;
  int x1 = x + rs::D6X[d], y1 = y + rs::D6Y[d], z1 = z + rs::D6Z[d];
  int x2 = x - rs::D6X[d], y2 = y - rs::D6Y[d], z2 = z - rs::D6Z[d];
  bool same = matcherSame(w.getBlock(x1, y1, z1), w.getMeta(x1, y1, z1), w.getBlock(x2, y2, z2), w.getMeta(x2, y2, z2));
  if (same == !!(m & 8)) return;
  w.setBlock(x, y, z, id, same ? (m | 8) : (m & ~8));
  queueAround(x, y, z);
  for (int k = 0; k < 6; k++) {
    int da = k < 2 ? 1 : k >= 4 ? 0 : 2;
    if (da == axis) continue;
    int nx = x + rs::D6X[k], ny = y + rs::D6Y[k], nz = z + rs::D6Z[k];
    queue(nx, ny, nz);
    queueAround(nx, ny, nz);
  }
}

// Pull an item through the one still-water face of a mesh. Velocity is blocks/tick, then scaled to blocks/second.
static double meshNoise(u32 id, int salt) {
  int h = (int)id * -1640531527 + salt * -2048144789;
  h ^= h >> 15; h *= 625341585; h ^= h >> 13;
  return (double)((unsigned)h >> 8) / 16777216.0;
}
static vec3 meshSiphonVel(int mx, int my, int mz, int dir, double x, double y, double z, double wander, double swirlJitter, double pullJitter) {
  int axis = dir < 2 ? 1 : dir >= 4 ? 0 : 2;
  double tx = mx + 0.5 + (axis == 0 ? 0.0 : wander);
  double ty = my + 0.5 + (axis == 1 ? 0.0 : wander * 0.5);
  double tz = mz + 0.5 + (axis == 2 ? 0.0 : -wander);
  double dx = tx - x, dy = ty - y, dz = tz - z;
  double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (dist <= 0.01) return vec3(0);
  const double maxRange = 2.5;
  double clamped = std::min(dist, maxRange);
  double prox = 1.0 + maxRange / (clamped + 0.5);
  double pull = 0.05 * pullJitter;
  double fade = std::min(1.0, dist / 0.75);
  double dyn = pull * 1.5 * prox * swirlJitter * fade;
  double speed = pull * prox;
  const double lateral = 3.0;
  double px = dx * speed * (axis == 0 ? 1.0 : lateral);
  double py = dy * speed * (axis == 1 ? 1.0 : lateral);
  double pz = dz * speed * (axis == 2 ? 1.0 : lateral);
  double sx = 0, sy = 0, sz = 0;
  if (axis == 1) { sx = -dz; sz = dx; }
  else if (axis == 2) { sx = -dy; sy = dx; }
  else { sy = -dz; sz = dy; }
  double sl = std::sqrt(sx * sx + sy * sy + sz * sz);
  if (sl > 0.01) { sx = sx / sl * dyn; sy = sy / sl * dyn; sz = sz / sl * dyn; }
  else sx = sy = sz = 0;
  double vx = px + sx, vy = py + sy, vz = pz + sz;
  double slow = 0.5 - 0.2 * (1.0 - clamped / maxRange);
  if (dir == 1 && vy < 0) vy *= slow;
  else if (dir == 0 && vy > 0) vy *= slow;
  else if (dir == 2 && vz > 0) vz *= slow;
  else if (dir == 3 && vz < 0) vz *= slow;
  else if (dir == 4 && vx > 0) vx *= slow;
  else if (dir == 5 && vx < 0) vx *= slow;
  return vec3((float)(vx * 0.75 * 20.0), (float)(vy * 0.75 * 20.0), (float)(vz * 0.75 * 20.0));
}

void Redstone::meshTick(int x, int y, int z, BlockId id, int) {
  const Registry& R = reg();
  const BlockDef& b = R.block(id);
  const bool gold = b.has("meshFilter");
  const json* be = w.blockEntity(x, y, z);
  std::string filter;
  int filterD = 0;
  if (gold) {
    if (!be || !be->is_object() || !be->contains("filter") || !(*be)["filter"].is_string()) return;
    filter = (*be)["filter"].get<std::string>();
    if (filter.empty()) return;
    filterD = be->value("filterD", 0);
  }
  int face = -1, faces = 0;
  for (int d = 1; d <= 5; d++) {
    int nx = x + rs::D6X[d], ny = y + rs::D6Y[d], nz = z + rs::D6Z[d];
    BlockId nid = w.getBlock(nx, ny, nz);
    if (R.tLiquid[nid] != 1) continue;
    int m = w.getMeta(nx, ny, nz);
    if ((m & 7) != 0 || (m & 8)) continue;
    face = d; faces++;
  }
  if (faces != 1 || face < 0) return;
  std::unordered_set<u64> reach;
  std::vector<ivec3> q;
  ivec3 start(x + rs::D6X[face], y + rs::D6Y[face], z + rs::D6Z[face]);
  q.push_back(start);
  reach.insert(rs::posKey(start.x, start.y, start.z));
  for (size_t i = 0; i < q.size() && (int)reach.size() < 50; i++) {
    ivec3 c = q[i];
    for (int d = 0; d < 6; d++) {
      if ((int)reach.size() >= 50) break;
      int nx = c.x + rs::D6X[d], ny = c.y + rs::D6Y[d], nz = c.z + rs::D6Z[d];
      if (R.tLiquid[w.getBlock(nx, ny, nz)] != 1) continue;
      if (!reach.insert(rs::posKey(nx, ny, nz)).second) continue;
      q.push_back(ivec3(nx, ny, nz));
    }
  }
  double cx = x + 0.5 + rs::D6X[face] * 0.5, cy = y + 0.5 + rs::D6Y[face] * 0.5, cz = z + 0.5 + rs::D6Z[face] * 0.5;
  double fx = cx + rs::D6X[face] * 2, fy = cy + rs::D6Y[face] * 2, fz = cz + rs::D6Z[face] * 2;
  double minX = std::min(cx, fx), minY = std::min(cy, fy), minZ = std::min(cz, fz);
  double maxX = std::max(cx, fx), maxY = std::max(cy, fy), maxZ = std::max(cz, fz);
  const double hw = 2.5;
  int axis = face < 2 ? 1 : face >= 4 ? 0 : 2;
  if (axis != 0) { minX -= hw; maxX += hw; }
  if (axis != 1) { minY -= hw; maxY += hw; }
  if (axis != 2) { minZ -= hw; maxZ += hw; }
  for (auto& up : lv.entities()) {
    Entity& e = *up;
    if (e.removed || e.realm != w.realm || e.type != "item") continue;
    ItemStack st = ItemStack::fromJson(e.data.contains("stack") ? e.data["stack"] : json());
    if (st.empty()) continue;
    if (gold && (st.id != filter || st.d != filterD)) continue;
    float iw = e.w > 0 ? e.w : 0.15f, ih = e.h > 0 ? e.h : 0.3f;
    if (e.pos.x + iw < minX || e.pos.x - iw > maxX || e.pos.y + ih < minY || e.pos.y > maxY || e.pos.z + iw < minZ || e.pos.z - iw > maxZ) continue;
    if (!reach.count(rs::posKey(ifloor(e.pos.x), ifloor(e.pos.y), ifloor(e.pos.z)))) continue;
    double phase = meshNoise(e.id, 1) * 6.283185307179586;
    double swirl = 0.8 + meshNoise(e.id, 3) * 0.4;
    double pull = 0.85 + meshNoise(e.id, 4) * 0.3;
    double wander = std::sin(e.age * 3.0 + phase) * 0.1;
    e.vel = meshSiphonVel(x, y, z, face, e.pos.x, e.pos.y, e.pos.z, wander, swirl, pull);
  }
}

} // namespace dq
