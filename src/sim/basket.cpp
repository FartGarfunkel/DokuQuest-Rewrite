// sim/basket.cpp — two small BTA ideas, our own designs:
// - the Wicker Basket: use it on a small animal (a chicken, rabbit, cat, fox, duck, frog...) to lift it in, on a block to
//   set it down again, as it was (its name, its owner, its colour). The animal rides in the stack's tag.
// - the Siphon: a copper pipe that draws items one by one out of what is behind it (a chest, a furnace, or loose items
//   lying there) and pushes them into what is in front (or out onto the ground), with a trickle of motes along it.
#include "sim/creature.h"
#include "sim/player.h"
#include "sim/controller.h"
#include "sim/gameplay.h"
#include "sim/redstone.h"
#include "core/registry.h"

namespace dq {
namespace {

bool fitsBasket(const Creature& c) {
  static const char* SMALL[] = {"rabbit", "chicken", "fox", "snow_fox", "raccoon", "cat", "ocelot", "armadillo", "mouse", "duck", "parrot",
                                "bee", "axolotl", "frog", "aerbunny", "turtle", "cod", "salmon", "tropical_fish", "pufferfish", "green_snake"};
  if (c.T->hostile && c.owner.empty()) return false;
  for (const char* k : SMALL) if (c.type == k) return true;
  return c.baby && !c.T->hostile;   // any baby animal fits
}

} // namespace

bool releaseBasket(PlayerController& pc, const RayHit& hit, ItemStack* held);

void registerBasket() {
  static bool done = false;
  if (done) return;
  done = true;
  hooks().useEntity.insert(hooks().useEntity.begin(), [](PlayerController& pc, Entity& e, ItemStack* held, const ItemDef*) {   // ahead of feeding and petting
    if (!held || held->id != "wicker_basket" || pc.lv.isRemote()) return false;
    auto* c = dynamic_cast<Creature*>(&e);
    if (!c || !c->alive()) return false;
    if (held->tag.is_object() && held->tag.contains("mob")) { toast(pc.lv, pc.p, "The basket already has someone in it"); return true; }
    if (!fitsBasket(*c)) { toast(pc.lv, pc.p, "That won't fit in a basket"); return true; }
    json j = c->save();
    held->tag = {{"mob", c->type}, {"c", j}, {"name", c->T->label.empty() ? c->type : c->T->label}};
    c->removed = true;
    pc.lv.fx->material("grass", "place", c->pos + vec3(0, 0.3f, 0));
    pc.swingArm();
    return true;
  });
  // set it down: on any block you look at (useItem when the block does nothing itself, useBlock when it does)
  hooks().useItem.push_back([](PlayerController& pc, ItemStack& held, const ItemDef& def, bool repeat) {
    if (repeat || def.key != "wicker_basket" || !pc.target.hit || pc.targetEnt) return false;
    return releaseBasket(pc, pc.target, &held);
  });
  hooks().useBlock.push_back([](PlayerController& pc, const RayHit& hit, ItemStack* held, const ItemDef*) { return releaseBasket(pc, hit, held); });
}

bool releaseBasket(PlayerController& pc, const RayHit& hit, ItemStack* held) {
  {
    if (!held || held->id != "wicker_basket" || pc.lv.isRemote() || !held->tag.is_object() || !held->tag.contains("mob")) return false;
    std::string type = held->tag.value("mob", std::string());
    vec3 at = vec3(hit.block) + vec3(0.5f, 0, 0.5f) + vec3(hit.normal);
    if (hit.normal.y == 0) at.y = (float)hit.block.y;
    Creature* c = spawnCreature(pc.lv, pc.p.realm, type, at);
    if (c && held->tag.contains("c")) {
      u32 id = c->id;
      c->load(held->tag["c"]);
      c->id = id; c->pos = c->prevPos = at; c->vel = vec3(0); c->removed = false;
    }
    held->tag = json();
    pc.lv.fx->material("grass", "break", at + vec3(0, 0.3f, 0));
    pc.swingArm();
    return true;
  }
}

// the siphon (blocks.json "siphon"): called by the redstone system's tickers every 8 ticks (sim/redstone.cpp)
void Redstone::siphonTick(int x, int y, int z, BlockId, int m) {
  static const int DX[4] = {0, 1, 0, -1}, DZ[4] = {-1, 0, 1, 0};   // 0 N 1 E 2 S 3 W: the way it pushes
  int f = m & 3;
  int fx = x + DX[f], fz = z + DZ[f], bx = x - DX[f], bz = z - DZ[f];
  vec3 c = vec3(x, y, z) + vec3(0.5f);
  vec3 dir((float)DX[f], 0, (float)DZ[f]);
  int outD6 = f == 0 ? 2 : f == 1 ? 5 : f == 2 ? 3 : 4, inD6 = OPP6[outD6];   // the D6 faces (sim/redstone.h D6X/D6Z)
  Container dst = inv(fx, y, fz), src = inv(bx, y, bz);
  ItemStack moving;
  if (src) {
    if (dst) { if (moveOne(src, inD6, dst, inD6)) { changedInv(src); changedInv(dst); moving = ItemStack("stick", 1); } }
    else if (src.items) {
      for (int i = 0; i < src.size(); i++) {
        ItemStack s = src.get(i);
        if (s.empty()) continue;
        ItemStack one = s; one.n = 1; s.n--; src.set(i, s); changedInv(src);
        dropItem(lv, w.realm, one, vec3(fx, y, fz) + vec3(0.5f, 0.3f, 0.5f), false, (const vec3*)nullptr);
        moving = one; break;
      }
    }
  } else {
    // loose items lying behind it
    for (auto& e : lv.entities()) {
      if (e->removed || e->type != "item" || e->realm != w.realm) continue;
      if (e->pos.x < bx - 0.2f || e->pos.x > bx + 1.2f || e->pos.z < bz - 0.2f || e->pos.z > bz + 1.2f || e->pos.y < y - 0.5f || e->pos.y > y + 1.5f) continue;
      ItemStack st = ItemStack::fromJson(e->data["stack"]);
      if (st.empty()) continue;
      ItemStack one = st; one.n = 1;
      bool ok = dst ? insertOne(dst, inD6, one) : true;
      if (!ok) continue;
      if (dst) changedInv(dst); else dropItem(lv, w.realm, one, vec3(fx, y, fz) + vec3(0.5f, 0.3f, 0.5f), false, (const vec3*)nullptr);
      st.n--; if (st.n <= 0) e->removed = true; else e->data["stack"] = st.toJson();
      moving = one; break;
    }
  }
  if (!moving.empty()) lv.fx->particles("end_mote", c + dir * 0.2f, 2);
}

} // namespace dq
