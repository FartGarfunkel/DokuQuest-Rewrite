// sim/pocket.cpp — the Pocket Key (game.js travelPocket, local.js pocketKey use): using it steps you into your own
// pocket dimension (REALM_POCKET, gen/realms.cpp PocketGen: one walled room per player every 256 blocks along x),
// and using it again takes you back to where you were (Player::pocketReturn {realm,x,y,z}).
#include "sim/player.h"
#include "sim/level.h"
#include "sim/creature.h"
#include "core/registry.h"
#include "gen/dimgens.h"

namespace dq {

namespace {
Player* namedPlayer(Level& lv, const std::string& n) {
  for (auto& e : lv.entities()) if (auto* p = dynamic_cast<Player*>(e.get())) if (!p->removed && p->name == n) return p;
  return nullptr;
}
// each player gets their own room: a slot kept in the level data by name, in order of first use
int pocketSlot(Level& lv, const std::string& name) {
  json& slots = lv.info.data["pocketSlots"];
  if (!slots.is_object()) slots = json::object();
  if (!slots.contains(name)) slots[name] = (int)slots.size();
  return slots[name].get<int>();
}
void place(Level& lv, Player& p, int realm, vec3 at) {
  if (p.riding) if (auto* m = dynamic_cast<Creature*>(lv.entity(p.riding))) dismount(lv, *m, p);
  lv.world(realm);   // make sure that world exists and streams
  p.realm = realm; p.pos = at; p.prevPos = at; p.vel = vec3(0); p.hasFallStart = false;
  lv.fx->sound("portal", at + vec3(0, 1, 0));
}
} // namespace

bool travelPocket(Level& lv, Player& p) {
  if (lv.isRemote()) return false;
  if (p.realm == REALM_POCKET) {
    json r = p.pocketReturn;
    vec3 at = r.is_object() && r.contains("x") ? vec3(r.value("x", 0.f), r.value("y", 0.f), r.value("z", 0.f))
                                                : vec3(lv.info.spawn.x, lv.info.spawn.y < 0 ? dimBase(DIM_OVERWORLD) + 90.f : lv.info.spawn.y, lv.info.spawn.z);
    int realm = r.is_object() ? r.value("realm", (int)REALM_MAIN) : REALM_MAIN;
    if (realm == REALM_POCKET) realm = REALM_MAIN;
    place(lv, p, realm, at);
    lv.fx->message(p.id, "Back from your pocket dimension", "");
  } else {
    p.pocketReturn = {{"realm", p.realm}, {"x", p.pos.x}, {"y", p.pos.y}, {"z", p.pos.z}};
    int slot = pocketSlot(lv, p.name);
    place(lv, p, REALM_POCKET, vec3(slot * 256 + 0.5f, (float)(dimBase(DIM_OVERWORLD) + PocketGen::FLOOR + 1), 0.5f));
    lv.fx->message(p.id, "Your pocket dimension \xE2\x80\x94 build and store anything here", "rare");
  }
  return true;
}

void registerPocket() {
  registerLevelSystem([](Level& lv) -> std::shared_ptr<LevelSystem> {
    struct PocketSystem : LevelSystem {
      Level* lv = nullptr; int h = 0;
      const char* name() const override { return "pocket"; }
      ~PocketSystem() override { if (lv && lv->events && h) lv->events->off(h); }
    };
    auto s = std::make_shared<PocketSystem>();
    s->lv = &lv;
    if (lv.events) s->h = lv.events->on("itemUse", [&lv](const json& a) {
      if (lv.isRemote() || !a.is_array() || a.size() < 2 || !a[1].is_string()) return true;
      const ItemDef* d = reg().item(a[1].get<std::string>());
      if (!d || !d->has("pocketKey")) return true;
      Player* p = a[0].is_string() ? namedPlayer(lv, a[0].get<std::string>()) : nullptr;
      if (!p || p->dead) return true;
      return !travelPocket(lv, *p);   // false = handled
    });
    return s;
  });
}

} // namespace dq
