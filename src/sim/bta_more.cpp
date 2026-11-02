// sim/bta_more.cpp — the rest of BTA's blocks and items (our own designs; data from tools/port/bta_more.py):
// - the Emberglass conduit: a run of conduits carries a redstone signal at full strength and glows while it does. It
//   takes power from switches, torches, redstone blocks, repeaters and the like touching it (not from dust or a powered
//   block, so a run can never hold itself on), and powers whatever touches it.
// - the matcher: a crystal comparator. It signals ahead while the container behind it holds the item set on it (use an
//   item on it; an empty hand clears it: then any item will do).
// - the gold mesh: like the iron mesh, but only the item set on it falls through (sim/physics.cpp reads "filter").
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
  if (clear) e->erase("filter"); else (*e)["filter"] = held->id;
  w.blockEntityChanged(hit.block.x, hit.block.y, hit.block.z);
  const ItemDef* d = clear ? nullptr : reg().item(held->id);
  std::string name = d ? d->name : clear ? "" : held->id;
  toast(pc.lv, pc.p, clear ? std::string(what == std::string("mesh") ? "The gold mesh lets nothing through now" : "The matcher looks for anything now")
                           : std::string(what == std::string("mesh") ? "Only " : "The matcher looks for ") + name + (what == std::string("mesh") ? " falls through" : ""), "info");
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
    if (pc.lv.isRemote()) return (b.has("meshFilter") || b.has("matcher") || (b.has("painting") && held && !held->empty())) && !pc.p.sneaking ? useOnHost(pc, "block", &hit) : false;
    if (b.has("meshFilter") && !pc.p.sneaking) return setFilter(pc, hit, held, "mesh");
    if (b.has("matcher") && !pc.p.sneaking) return setFilter(pc, hit, held, "matcher");
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

using rs::Tables; using rs::T; using rs::CONDUIT; using rs::WIRE;

// ---- the Emberglass conduit: the run lights as one, from what feeds it from outside
void Redstone::updateConduit(int x, int y, int z) {
  const Tables& t = T();
  std::vector<ivec3> run, todo{ivec3(x, y, z)};
  std::unordered_set<u64> seen{rs::posKey(x, y, z)};
  bool fed = false;
  while (!todo.empty() && run.size() < 512) {
    ivec3 c = todo.back(); todo.pop_back();
    if (t.role[w.getBlock(c.x, c.y, c.z)] != CONDUIT) continue;
    run.push_back(c);
    for (int d = 0; d < 6; d++) {
      int nx = c.x + rs::D6X[d], ny = c.y + rs::D6Y[d], nz = c.z + rs::D6Z[d];
      BlockId id = w.getBlock(nx, ny, nz);
      u8 r = t.role[id];
      if (r == CONDUIT) { if (seen.insert(rs::posKey(nx, ny, nz)).second) todo.push_back(ivec3(nx, ny, nz)); continue; }
      if (!fed && r && r != WIRE && emit(nx, ny, nz, id, w.getMeta(nx, ny, nz), rs::OPP6[d]) > 0) fed = true;
      if (!fed && id == t.oreLit && id) fed = true;   // glowing redstone ore feeds it too (BTA's conduits drew on redstone ore)
    }
  }
  for (const ivec3& c : run) {
    BlockId id = w.getBlock(c.x, c.y, c.z);
    if (!!t.conduitLit[id] != fed && t.conduitFlip[id]) w.setBlock(c.x, c.y, c.z, t.conduitFlip[id], w.getMeta(c.x, c.y, c.z));
  }
}

// ---- the matcher: on while the container behind it holds its item
void Redstone::matcherTick(int x, int y, int z, BlockId id, int m) {
  const int out = rs::H2D6[m & 3], back = rs::OPP6[out];
  Container src = inv(x + rs::D6X[back], y + rs::D6Y[back], z + rs::D6Z[back]);
  const json* e = w.blockEntity(x, y, z);
  std::string want = e && e->is_object() && e->contains("filter") && (*e)["filter"].is_string() ? (*e)["filter"].get<std::string>() : "";
  bool on = false;
  if (src && src.items) for (int i = 0; i < src.size() && !on; i++) { ItemStack s = src.get(i); if (!s.empty() && (want.empty() || s.id == want)) on = true; }
  if (on == !!(m & 8)) return;
  w.setBlock(x, y, z, id, on ? m | 8 : m & ~8);
  queueAround(x, y, z);
  const int fx = x + rs::D6X[out], fy = y + rs::D6Y[out], fz = z + rs::D6Z[out];
  queue(fx, fy, fz); queueAround(fx, fy, fz);
}

} // namespace dq
