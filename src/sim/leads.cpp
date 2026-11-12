// sim/leads.cpp — the lead: use it on an animal and it follows you (sim/creature_beasts.cpp controlAI walks it after
// you, and past 6 blocks it is dragged); use a fence post while leading animals to tie them there, and the post again
// to take them back; use the animal again to let it go. Past 10 blocks the lead snaps and drops. The rope is drawn by
// render/content_layers.cpp.
#include "sim/leads.h"
#include "sim/creature.h"
#include "sim/player.h"
#include "sim/controller.h"
#include "sim/gameplay.h"
#include "sim/physics.h"
#include "world/shapes.h"
#include "core/registry.h"

namespace dq {

namespace {
Player* playerNamed(Level& lv, const std::string& n) {
  for (auto& e : lv.entities()) if (auto* p = dynamic_cast<Player*>(e.get())) if (!p->removed && p->name == n) return p;
  return nullptr;
}
bool onLead(const Entity& e) { return e.data.is_object() && e.data.contains("leash"); }
void letGo(Level& lv, Creature& c, bool drop) {
  c.data.erase("leash");
  if (drop) dropItem(lv, c.realm, ItemStack("lead", 1), c.pos + vec3(0, c.h * 0.6f, 0), true, nullptr, 0.f);
}
bool leashable(const Creature& c) { return c.alive() && !c.ghost && !c.T->hostile && !c.T->boss && !c.T->npc && !c.T->ambient && !c.T->spirit; }
bool isPost(World& w, const json& L) {
  if (!L.is_array() || L.size() != 3) return false;
  return reg().tShape[w.getBlock(L[0].get<int>(), L[1].get<int>(), L[2].get<int>())] == ShapeKind::Fence;
}
} // namespace

bool leashHolder(Level& lv, const Entity& e, vec3& at) {
  if (!onLead(e)) return false;
  const json& L = e.data["leash"];
  if (L.is_string()) {
    Player* p = playerNamed(lv, L.get<std::string>());
    if (!p || p->dead || p->realm != e.realm) return false;
    at = p->pos + vec3(0, 1.1f, 0);
    return true;
  }
  if (!isPost(lv.world(e.realm), L)) return false;
  at = vec3(L[0].get<int>() + 0.5f, L[1].get<int>() + 0.6f, L[2].get<int>() + 0.5f);
  return true;
}

void registerLeads() {
  static bool done = false;
  if (done) return;
  done = true;
  // ahead of the other creature uses (feeding, riding...), as Minecraft does
  hooks().useEntity.insert(hooks().useEntity.begin(), [](PlayerController& pc, Entity& e, ItemStack* held, const ItemDef*) {
    auto* c = dynamic_cast<Creature*>(&e);
    if (!c || pc.lv.isRemote()) return false;
    Player& p = pc.p;
    if (onLead(*c)) {
      const json& L = c->data["leash"];
      if (L.is_string() && L.get<std::string>() == p.name) { letGo(pc.lv, *c, !p.creative()); pc.lv.fx->material("wool", "break", c->pos); return true; }
      if (L.is_array()) { c->data["leash"] = p.name; pc.lv.fx->material("wool", "place", c->pos); return true; }   // off the post, into your hand
      return false;
    }
    if (!held || held->id != "lead" || !leashable(*c)) return false;
    c->data["leash"] = p.name;
    c->stay = false; c->sitting = false; c->sleeping = false;
    pc.consumeHeld();
    pc.lv.fx->material("wool", "place", c->pos + vec3(0, c->h * 0.6f, 0));
    return true;
  });
  hooks().useBlock.push_back([](PlayerController& pc, const RayHit& hit, ItemStack*, const ItemDef*) {
    if (pc.lv.isRemote() || reg().tShape[hit.id] != ShapeKind::Fence) return false;
    Level& lv = pc.lv; Player& p = pc.p;
    json post = json::array({hit.block.x, hit.block.y, hit.block.z});
    int tied = 0, taken = 0;
    for (auto& e : lv.entities()) {   // tie up whatever you lead...
      auto* c = dynamic_cast<Creature*>(e.get());
      if (!c || c->removed || c->realm != p.realm || !onLead(*c)) continue;
      const json& L = c->data["leash"];
      if (L.is_string() && L.get<std::string>() == p.name) { c->data["leash"] = post; tied++; }
    }
    if (!tied) for (auto& e : lv.entities()) {   // ...or take back what's tied here
      auto* c = dynamic_cast<Creature*>(e.get());
      if (!c || c->removed || c->realm != p.realm || !onLead(*c)) continue;
      if (c->data["leash"] == post) { c->data["leash"] = p.name; taken++; }
    }
    if (!tied && !taken) return false;
    lv.fx->material("wool", "place", vec3(hit.block) + vec3(0.5f));
    pc.swingArm();
    return true;
  });
  registerLevelSystem([](Level&) -> std::shared_ptr<LevelSystem> {
    struct S : LevelSystem {
      const char* name() const override { return "leads"; }
      void tick(Level& lv, double dtd) override {
        if (lv.isRemote()) return;
        float dt = (float)dtd;
        std::vector<Creature*> snap;   // let go after the walk: the dropped lead joins the entity list
        for (auto& e : lv.entities()) {
          auto* c = dynamic_cast<Creature*>(e.get());
          if (!c || c->removed || !onLead(*c)) continue;
          vec3 h;
          if (!c->alive() || !leashHolder(lv, *c, h)) { snap.push_back(c); continue; }
          vec3 neck = c->pos + vec3(0, c->h * 0.6f, 0);
          float d = glm::distance(h, neck);
          if (d > 10) { snap.push_back(c); lv.fx->material("wool", "break", neck); continue; }   // it snaps
          if (d > 6) {   // dragged along
            vec3 dir = (h - neck) / d;
            moveBox(lv.world(c->realm), c->pos, c->w, c->h, dir * std::min((d - 6) * 5.f, 12.f) * dt);
            if (dir.y > 0.3f && c->onGround) c->vel.y = std::max(c->vel.y, 4.f);
          }
        }
        for (Creature* c : snap) letGo(lv, *c, true);
      }
    };
    return std::make_shared<S>();
  });
}

} // namespace dq
