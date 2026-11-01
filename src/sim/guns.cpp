// sim/guns.cpp — powder weapons (BTA's handcannon and rocket launcher, our own make): the handcannon fires a cannonball
// that bursts where it lands without breaking blocks (fire it at your feet to fling yourself), the rocket launcher a
// rocket that flies straight and blasts what it hits. Both kick back. The shots are projectiles in sim/creature_entities.cpp.
#include "sim/gameplay.h"
#include "sim/controller.h"
#include "sim/player.h"
#include "sim/inventory.h"
#include "sim/creature_entities.h"
#include "core/registry.h"
#include <unordered_map>

namespace dq {

void registerGuns() {
  static bool done = false;
  if (done) return;
  done = true;
  hooks().useItem.push_back([](PlayerController& pc, ItemStack& held, const ItemDef& def, bool repeat) {
    const bool cannon = def.key == "handcannon", launcher = def.key == "rocket_launcher";
    if (repeat || (!cannon && !launcher)) return false;
    Level& lv = pc.lv; Player& p = pc.p;
    static std::unordered_map<u32, i64> next;
    const i64 now = unixMillis();
    if (next[p.id] > now) return true;
    const char* ammo = cannon ? "cannonball" : "rocket";
    if (!p.creative()) {
      if (playerCount(p, ammo) <= 0) { pc.toastMsg(cannon ? "You need cannonballs (an iron ingot and gunpowder)" : "You need rockets"); return true; }
      playerRemove(p, ammo, 1);
    }
    next[p.id] = now + (cannon ? 900 : 1600);
    const vec3 L = lookVector(p.yaw, p.pitch);
    const vec3 from = p.pos + vec3(0, p.eye - 0.15f, 0) + L * 0.6f;
    spawnProjectile(lv, p.realm, from, L * (cannon ? 26.f : 22.f) + (cannon ? vec3(0, 1.5f, 0) : vec3(0)), ammo, &p);
    p.vel -= L * (cannon ? 3.5f : 5.f);   // the kick
    lv.fx->sound("explode", from, cannon ? 18 : 24);
    lv.fx->particles("smoke", from + L * 0.4f, cannon ? 6 : 10);
    pc.swingArm();
    if (!p.creative() && def.hasTool && def.tool.durability > 0 && ++held.d >= def.tool.durability) { pc.toastMsg("Your " + def.name + " broke", "warn"); held.clear(); }
    return true;
  });
}

} // namespace dq
