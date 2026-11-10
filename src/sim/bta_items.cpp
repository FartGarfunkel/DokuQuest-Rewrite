// sim/bta_items.cpp — what Better than Adventure 8.0.1's newer items do (the items themselves: tools/port/bta_extras.py):
//   the steel bucket (ItemBucketSteel: three bucketfuls, kept on the stack's tag {fluid, n}; scoop from a source, pour
//   one at a time), wolf armour (on your own tamed wolf: it takes it, protects it like a player's armour, wears down;
//   sneak with an empty hand to take it back), and the label (ItemLabel: write a name on it, then use it on a creature
//   to give it that name). The bow's golden / flaming / purple arrows are in sim/controller.cpp, the explosive charge
//   in sim/guns.cpp, the ice skates in sim/player.cpp, the calendar's readout in ui/hud.cpp.
#include "sim/gameplay.h"
#include "sim/controller.h"
#include "sim/creature.h"
#include "sim/player.h"
#include "sim/inventory.h"
#include "core/registry.h"
#include "world/world.h"
#include "sim/physics.h"
#include "common/noise.h"
#include "sim/mob_combat.h"
#include "sim/effects.h"

namespace dq {

void openContentScreen(Level& lv, const Player& p, const std::string& id, const json& args);   // sim/content.cpp

namespace {
// ---- the steel bucket ----
bool steelBucket(PlayerController& pc, ItemStack& held, const ItemDef& def) {
  Player& p = pc.p; Level& lv = pc.lv; World& w = lv.world(p.realm);
  const int cap = (int)def.num("charges", 3);
  if (!held.tag.is_object()) held.tag = json::object();
  std::string fluid = held.tag.value("fluid", std::string()); int n = held.tag.value("n", 0);
  auto save = [&] {
    if (n <= 0) { held.tag.erase("fluid"); held.tag.erase("n"); held.tag.erase("name"); if (held.tag.empty()) held.tag = nullptr; return; }
    held.tag["fluid"] = fluid; held.tag["n"] = n;
    held.tag["name"] = std::string("Steel Bucket of ") + (fluid == "water" ? "Water" : "Lava") + " (" + std::to_string(n) + " of " + std::to_string(cap) + ")";
  };
  const vec3 L = lookVector(p.yaw, p.pitch);
  RayHit h = raycastBlocks(w, glm::dvec3(p.pos.x, p.pos.y + p.eye, p.pos.z), L, 5, true);
  const Registry& R = reg();
  // scoop: a still source of the same fluid (or any, when it's empty), while there's room
  if (h.hit && R.tLiquid[h.id] && (w.getMeta(h.block.x, h.block.y, h.block.z) & 15) == 0 && n < cap) {
    if (R.tLiquid[h.id] == 3) { pc.toastMsg("The acid would eat through the bucket"); return true; }
    std::string f = R.tLiquid[h.id] == 1 ? "water" : "lava";
    if (n == 0 || f == fluid) {
      w.setBlock(h.block.x, h.block.y, h.block.z, 0, 0, SetFlags::Urgent);
      fluid = f; n++; save();
      sfxUi(lv, p, "bucket"); pc.swingArm();
      return true;
    }
  }
  // pour one
  if (n > 0 && pc.target.hit) {
    ivec3 t = pc.placeTarget(pc.target);
    BlockId cur = w.getBlock(t.x, t.y, t.z);
    if (cur && !R.tReplaceable[cur]) return true;
    if (fluid == "water" && p.realm == REALM_MAIN && dimOfY(t.y) == DIM_NETHER) { sfxUi(lv, p, "fizz"); n--; save(); return true; }
    w.setBlock(t.x, t.y, t.z, R.blockId(fluid == "water" ? "water" : "lava"), 0, SetFlags::Urgent);
    if (!p.creative()) { n--; save(); }
    sfxUi(lv, p, "bucket"); pc.swingArm();
  }
  return true;
}
}

Entity* spawnProjectile(Level& lv, int realm, vec3 p, vec3 v, const std::string& kind, Entity* owner);   // sim/creature_entities.cpp
int seasonOf(const Level& lv);   // sim/weather.cpp

// BTA's leaf piles: in autumn the trees round people shed them onto the ground under their leaves
struct LeafFall : LevelSystem {
  double t = 0;
  const char* name() const override { return "leaf_fall"; }
  void tick(Level& lv, double dt) override {
    if (lv.isRemote() || (t += dt) < 2) return;
    t = 0;
    if (seasonOf(lv) != 2) return;
    static const BlockId pile = reg().blockId("leaf_pile");
    if (!pile) return;
    World& w = lv.world(REALM_MAIN);
    for (auto& e : lv.entities()) {
      auto* p = dynamic_cast<Player*>(e.get());
      if (!p || p->removed || p->realm != REALM_MAIN || dimOfY(ifloor(p->pos.y)) != DIM_OVERWORLD) continue;
      for (int k = 0; k < 6; k++) {
        const u32 h = hash32(ifloor(p->pos.x), ifloor(lv.info.time * 10), k, 0x1EAF);
        const int x = ifloor(p->pos.x) + (int)(h % 49) - 24, z = ifloor(p->pos.z) + (int)((h >> 8) % 49) - 24;
        bool underLeaves = false;
        for (int y = ifloor(p->pos.y) + 20; y > ifloor(p->pos.y) - 20; y--) {
          if (!w.isLoaded(x, y, z)) break;
          const BlockId b = w.getBlock(x, y, z);
          if (b == AIR) continue;
          if (reg().block(b).has("isLeaves")) { underLeaves = true; continue; }
          if (underLeaves && reg().tSolid[b] && !reg().tShape[b] && w.getBlock(x, y + 1, z) == AIR && ((h >> 16) & 3) == 0) w.setBlock(x, y + 1, z, pile, 0);
          break;
        }
      }
    }
  }
};

// BTA's Giant, a crypt's boss here (tools/port/bta_extras.py): with someone close it rears up for a moment (time to get
// clear), then stamps — those still near are hurt and thrown up. mobs.json "stomp".
struct GiantStomp : LevelSystem {
  double t = 0;
  const char* name() const override { return "giant_stomp"; }
  void tick(Level& lv, double dt) override {
    if (lv.isRemote() || (t += dt) < 0.1) return;
    const float step = (float)t; t = 0;
    for (auto& e : lv.entities()) {
      auto* g = dynamic_cast<Creature*>(e.get());
      if (!g || g->removed || g->hp <= 0 || !g->T || !g->T->j.value("stomp", false)) continue;
      auto nearOnes = [&](float R) {
        std::vector<Entity*> v;
        for (Entity* q : pawnsOf(lv, g->realm))
          if (q != g && std::hypot(q->pos.x - g->pos.x, q->pos.z - g->pos.z) < R && std::fabs(q->pos.y - g->pos.y) < 2.5f) v.push_back(q);
        return v;
      };
      if (g->data.contains("stompWind")) {   // reared up: down it comes
        const float w = g->data.value("stompWind", 0.f) - step;
        if (w > 0) { g->data["stompWind"] = w; g->vel.x *= 0.5f; g->vel.z *= 0.5f; continue; }
        g->data.erase("stompWind");
        lv.fx->sound("slam", g->pos, 40);
        lv.fx->particles("smoke", g->pos + vec3(0, 0.3f, 0), 36);
        for (Entity* q : nearOnes(4.8f)) {
          DamageSrc s; s.knock = 9;
          damageEntity(lv, *q, 7, g, "mob", s);
          knockback(*q, vec3(0, 7.5f, 0));
        }
        for (Entity* q : pawnsOf(lv, g->realm)) if (glm::distance(q->pos, g->pos) < 20) lv.fx->shake(q->id, 0.6f);
        continue;
      }
      const float cd = g->data.value("stompT", 3.f) - step;
      if (cd > 0) { g->data["stompT"] = cd; continue; }
      if (nearOnes(4.2f).empty()) { g->data["stompT"] = 0.4f; continue; }
      g->data["stompWind"] = 0.75f;
      g->data["stompT"] = 4.f + (float)(hash32((int)g->id, (int)(lv.info.time * 10), 5, 1) % 200) / 100.f;
      lv.fx->sound("warn", g->pos, 32);
    }
  }
};

void registerBtaItems() {
  static bool done = false;
  if (done) return;
  done = true;
  registerLevelSystem([](Level&) -> std::shared_ptr<LevelSystem> { return std::make_shared<LeafFall>(); });
  registerLevelSystem([](Level&) -> std::shared_ptr<LevelSystem> { return std::make_shared<GiantStomp>(); });
  // BTA's statues: two blocks tall (the lower places the upper above it; break either and both go)
  hooks().blockChanged.push_back([](Level& lv, World& w, int x, int y, int z, BlockId oldId, BlockId newId) {
    if (lv.isRemote()) return;
    const Registry& R = reg();
    if (newId) if (const std::string up = R.block(newId).str("statueLower"); !up.empty()) {
      if (w.getBlock(x, y + 1, z) == AIR || R.tReplaceable[w.getBlock(x, y + 1, z)]) w.setBlock(x, y + 1, z, R.blockId(up), w.getMeta(x, y, z));
      return;
    }
    if (!oldId || oldId == newId) return;
    if (const std::string up = R.block(oldId).str("statueLower"); !up.empty() && w.getBlock(x, y + 1, z) == R.blockId(up)) w.setBlock(x, y + 1, z, AIR);
    if (const std::string lo = R.block(oldId).str("statueUpper"); !lo.empty() && w.getBlock(x, y - 1, z) == R.blockId(lo)) w.setBlock(x, y - 1, z, AIR);
  });
  hooks().useItem.push_back([](PlayerController& pc, ItemStack& held, const ItemDef& def, bool repeat) {
    if (repeat) return false;
    if (def.has("charges") && def.has("bucket")) return steelBucket(pc, held, def);
    if (def.key == "pebble" && !pc.target.hit) {   // BTA's pebble, thrown when there's nothing to set it on
      Player& p = pc.p; const vec3 L = lookVector(p.yaw, p.pitch);
      spawnProjectile(pc.lv, p.realm, p.pos + vec3(0, p.eye - 0.1f, 0) + L * 0.5f, L * 22.f + vec3(0, 1.5f, 0), "snow", &p);
      if (!p.creative() && --held.n <= 0) held.clear();
      sfxUi(pc.lv, p, "throw"); pc.swingArm();
      return true;
    }
    if (def.key == "goat_horn") {   // a war horn: you and the people round you, quicker and stronger for a while
      Player& p = pc.p; Level& lv = pc.lv;
      const i64 now = unixMillis(), ready = p.stats.value("hornAt", (i64)0);
      if (now < ready) { pc.toastMsg("Your breath's not back yet (" + std::to_string((ready - now + 999) / 1000) + " s)"); return true; }
      p.stats["hornAt"] = now + 30000;
      lv.fx->sound("roar", p.pos + vec3(0, 1.6f, 0), 64, 1.f, 1.35f);
      for (auto& o : lv.entities()) if (auto* q = dynamic_cast<Player*>(o.get())) if (!q->removed && !q->dead && q->realm == p.realm && glm::distance(q->pos, p.pos) < 16) {
        giveEffect(lv, *q, "speed", 10, 0); giveEffect(lv, *q, "strength", 10, 0);
        if (q != &p) lv.fx->message(q->id, p.name + " sounds the horn!", "good");
      }
      pc.toastMsg("You sound the horn: quicker and stronger, you and those round you");
      pc.swingArm();
      return true;
    }
    if (def.has("label") && !(held.tag.is_object() && held.tag.contains("name"))) {   // a blank label: write on it
      openContentScreen(pc.lv, pc.p, "label", {{"slot", pc.p.selected}});
      return true;
    }
    return false;
  });
  hooks().useEntity.push_back([](PlayerController& pc, Entity& e, ItemStack* held, const ItemDef* def) {
    Player& p = pc.p; Level& lv = pc.lv;
    auto* c = dynamic_cast<Creature*>(&e);
    if (!c) return false;
    // wolf armour: on your own tamed wolf
    if (def && def->props.contains("wolfArmor") && held) {
      if (c->type != "wolf" || !c->tamed || c->owner != p.name) { pc.toastMsg("Wolf armour goes on a wolf you've tamed"); return true; }
      if (c->data.contains("wolfArmor") && c->data["wolfArmor"].is_string()) { ItemStack old(c->data["wolfArmor"].get<std::string>(), 1); old.d = c->data.value("wolfArmorD", 0); if (p.give(old) > 0) dropItem(lv, p.realm, old, c->pos, false, (const vec3*)nullptr); }
      c->data["wolfArmor"] = held->id; c->data["wolfArmorD"] = held->d;
      if (!p.creative() && --held->n <= 0) held->clear();
      sfxAt(lv, "equip", c->pos); pc.swingArm();
      return true;
    }
    if ((!held || held->empty()) && p.sneaking && c->type == "wolf" && c->owner == p.name && c->data.contains("wolfArmor") && c->data["wolfArmor"].is_string()) {
      ItemStack old(c->data["wolfArmor"].get<std::string>(), 1); old.d = c->data.value("wolfArmorD", 0);
      c->data.erase("wolfArmor"); c->data.erase("wolfArmorD");
      if (p.give(old) > 0) dropItem(lv, p.realm, old, c->pos, false, (const vec3*)nullptr);
      sfxAt(lv, "equip", c->pos); pc.swingArm();
      return true;
    }
    // a label with a name on it names the creature
    if (def && def->has("label") && held && held->tag.is_object() && held->tag.contains("name") && !c->isPlayer()) {
      c->petName = held->tag["name"].get<std::string>().substr(0, 32);
      c->data["name"] = c->petName;
      if (!p.creative() && --held->n <= 0) held->clear();
      pc.toastMsg("It's called " + c->petName + " now");
      pc.swingArm();
      return true;
    }
    return false;
  });
}

// wolf armour's protection points (Creature::armourPoints adds them) and its wear (Creature::hurt)
float wolfArmourPoints(const Creature& c) {
  if (!c.data.contains("wolfArmor") || !c.data["wolfArmor"].is_string()) return 0;
  const ItemDef* d = reg().item(c.data["wolfArmor"].get<std::string>());
  return d && d->props.contains("wolfArmor") ? d->props["wolfArmor"].value("points", 0.f) : 0.f;
}
void wolfArmourWear(Creature& c, Level& lv) {
  if (!c.data.contains("wolfArmor") || !c.data["wolfArmor"].is_string()) return;
  const ItemDef* d = reg().item(c.data["wolfArmor"].get<std::string>());
  const int dur = d && d->props.contains("wolfArmor") ? d->props["wolfArmor"].value("durability", 0) : 0;
  const int used = c.data.value("wolfArmorD", 0) + 1;
  if (dur > 0 && used >= dur) { c.data.erase("wolfArmor"); c.data.erase("wolfArmorD"); lv.fx->sound("break_tool", c.pos, 16); }
  else c.data["wolfArmorD"] = used;
}

} // namespace dq
