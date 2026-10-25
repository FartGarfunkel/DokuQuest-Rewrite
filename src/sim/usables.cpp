// sim/usables.cpp — items the original used through its "drink: milk" hook that nothing else here handled:
// firework rockets sent up from the ground (batche.js FireworkRocket; the elytra boost is sim/elytra.cpp),
// the gust puff's great leap (aether.js), and the Ember Effigy that wakes a Cinder Golem at night (local.js summon).
#include "sim/creature.h"
#include "sim/player.h"
#include "sim/effects.h"
#include "core/registry.h"
#include "sim/creature_entities.h"
#include <unordered_map>
#include <chrono>
#include "sim/content.h"

namespace dq {

namespace {
Player* named(Level& lv, const std::string& n) {
  for (auto& e : lv.entities()) if (auto* p = dynamic_cast<Player*>(e.get())) if (!p->removed && p->name == n) return p;
  return nullptr;
}
void useUp(Player& p) { ItemStack& h = p.held(); if (--h.n <= 0) h.clear(); }

class FireworkRocket : public Entity {
public:
  FireworkRocket() { type = "firework"; w = 0.1f; h = 0.2f; noGravity = true; }
  void tick(Level& lv, double dtd) override {
    float dt = (float)dtd;
    float fuse = data.value("fuse", 1.3f);
    vel.y = std::min(vel.y + 8 * dt, 22.f);
    vec3 n = pos + vel * dt;
    World& w = lv.world(realm);
    if (age >= fuse || reg().tSolid[w.getBlock(ifloor(n.x), ifloor(n.y + 0.2f), ifloor(n.z))]) {
      json ex = json::object(); std::string c = data.value("color", std::string()); if (!c.empty()) ex["color"] = c;
      lv.fx->particles("firework", pos, 60, ex);
      lv.fx->sound("firework_blast", pos, 90);
      if (rand() & 1) lv.fx->sound("firework_twinkle", pos, 70);
      removed = true; return;
    }
    pos = n;
    lv.fx->particles("flame", pos - vec3(0, 0.15f, 0), 1);
  }
};
} // namespace

void registerUsables() {
  registerEntityFactory("firework", [](const std::string&) { return std::make_unique<FireworkRocket>(); });
  registerLevelSystem([](Level& lv) -> std::shared_ptr<LevelSystem> {
    struct S : LevelSystem {
      Level* lv = nullptr; int h = 0;
      const char* name() const override { return "usables"; }
      ~S() override { if (lv && lv->events && h) lv->events->off(h); }
    };
    auto s = std::make_shared<S>(); s->lv = &lv;
    if (lv.events) s->h = lv.events->on("itemUse", [&lv](const json& a) {   // [player, item, x, y, z, face]
      if (lv.isRemote() || !a.is_array() || a.size() < 6 || !a[1].is_string() || !a[0].is_string()) return true;
      const ItemDef* d = reg().item(a[1].get<std::string>());
      if (!d) return true;
      Player* p = named(lv, a[0].get<std::string>());
      if (!p || p->dead) return true;
      bool hitBlock = a[2].is_number() && a[5].is_number() && a[5].get<int>() >= 0;
      ivec3 hb = hitBlock ? ivec3(a[2].get<int>(), a[3].get<int>(), a[4].get<int>()) : ivec3(0);
      if (d->has("firework") && !p->gliding) {
        if (!hitBlock) return true;
        auto e = std::make_unique<FireworkRocket>();
        e->realm = p->realm; e->pos = vec3(hb) + vec3(0.5f, 1.1f, 0.5f);
        e->vel = vec3((rand() % 100 - 50) / 125.f, 10, (rand() % 100 - 50) / 125.f);
        e->data = {{"fuse", 1.1f + (rand() % 60) / 100.f}, {"color", d->str("fwColor")}};
        lv.fx->sound("firework_launch", e->pos, 40);
        lv.spawn(std::move(e));
        if (!p->creative()) useUp(*p);
        return false;
      }
      if (d->key == "egg" || d->key == "slime_ball") {   // thrown by hand like snowballs (theend.js; the End story throws those)
        static std::unordered_map<u32, double> cd;
        double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (cd[p->id] > now) return false;
        cd[p->id] = now + 0.25;
        vec3 lk = lookVec(*p);
        vec3 eye = p->pos + vec3(0, 1.62f, 0);
        spawnProjectile(lv, p->realm, eye - vec3(0, 0.1f, 0) + lk * 0.5f, lk * 18.f + vec3(0, 2, 0), d->key == "egg" ? "egg" : d->key == "slime_ball" ? "slime" : "snow", p);
        if (!p->creative()) useUp(*p);
        p->swingT = 0.01f; lv.fx->soundUi(p->id, "bow");
        return false;
      }
      if (d->has("gustPuff")) {
        if (!p->creative()) useUp(*p);
        p->vel.y = std::max(p->vel.y, 12.5f); p->onGround = false; p->hasFallStart = false;
        giveEffect(lv, *p, "slowfall", 3);
        lv.fx->particles("poof", p->pos + vec3(0, 0.1f, 0), 18);
        lv.fx->sound("wind_charge", p->pos, 24);
        return false;
      }
      if (d->has("summon")) {
        if (!hitBlock || a[5].get<int>() != 2) { lv.fx->message(p->id, "Place the effigy on top of a block"); return false; }
        for (auto& e : lv.entities()) if (auto* c = dynamic_cast<Creature*>(e.get())) if (c->type == "cinder_golem" && c->alive()) { lv.fx->message(p->id, "A Cinder Golem already walks this world", "boss"); return false; }
        int dim = dimOfY(ifloor(p->pos.y));
        if (p->realm == REALM_MAIN && dim == DIM_OVERWORLD && envDay(lv, dim) > 0.45f) { lv.fx->message(p->id, "The effigy lies cold. It only stirs at night"); return false; }
        useUp(*p);
        summonGolem(lv, p->realm, vec3(hb) + vec3(0.5f, 1, 0.5f), p);
        return false;
      }
      return true;
    });
    return s;
  });
}

} // namespace dq
