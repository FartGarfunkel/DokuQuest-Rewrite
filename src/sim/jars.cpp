// sim/jars.cpp — glass jars (BTA's jar): an empty jar swung through the air over grass at night catches fireflies
// (the same nights and places render/creature_fauna.cpp sends them out); a placed jar of fireflies glows, and using
// it lets them go. The fireflies drifting inside are render/env_layers.cpp's.
#include "sim/gameplay.h"
#include "sim/controller.h"
#include "sim/creature.h"
#include "sim/player.h"
#include "core/registry.h"
#include "sim/physics.h"

namespace dq {

void registerJars() {
  static bool done = false;
  if (done) return;
  done = true;
  hooks().useItem.push_back([](PlayerController& pc, ItemStack& held, const ItemDef& def, bool repeat) {
    if (repeat || def.key != "glass_jar" || pc.target.hit || pc.targetEnt) return false;
    if (pc.lv.isRemote()) return useOnHost(pc, "item");
    Level& lv = pc.lv; Player& p = pc.p;
    World& w = lv.world(p.realm);
    int px = ifloor(p.pos.x), py = ifloor(p.pos.y), pz = ifloor(p.pos.z);
    const bool over = p.realm == REALM_MAIN && dimOfY(py) == DIM_OVERWORLD;
    const float day = envDay(lv, DIM_OVERWORLD);
    bool night = over && day < 0.45f, bright = over && day > 0.6f;   // fireflies by night, butterflies on bright days
    static BlockId grass = reg().blockId("grass");
    bool meadow = false;
    for (int dz = -2; dz <= 2 && !meadow; dz++) for (int dx = -2; dx <= 2 && !meadow; dx++) for (int dy = -3; dy <= 0; dy++)
      if (w.getBlock(px + dx, py + dy, pz + dz) == grass) { meadow = true; break; }
    pc.swingArm();
    if ((!night && !bright) || !meadow) { pc.toastMsg(!meadow ? "Nothing flits about here: try over grass" : "Fireflies come out over grass at night, butterflies on bright days"); return true; }
    const char* got = night ? "firefly_jar" : "butterfly_jar";
    if (--held.n <= 0) held.clear();
    if (giveItem(lv, p, ItemStack(got, 1)) > 0) dropItem(lv, p.realm, ItemStack(got, 1), p.pos + vec3(0, 1, 0), true, nullptr, 0.f);
    if (night) lv.fx->particles("firefly", p.pos + vec3(0, 1.2f, 0) + lookVector(p.yaw, p.pitch) * 1.2f, 6);
    pc.toastMsg(night ? "You caught some fireflies" : "You caught some butterflies", "good");
    return true;
  });
  hooks().useBlock.push_back([](PlayerController& pc, const RayHit& hit, ItemStack*, const ItemDef*) {
    static BlockId full = reg().blockId("firefly_jar"), flutter = reg().blockId("butterfly_jar"), empty = reg().blockId("glass_jar");
    if ((!full || hit.id != full) && (!flutter || hit.id != flutter)) return false;
    if (pc.lv.isRemote()) return useOnHost(pc, "block", &hit);
    World& w = pc.lv.world(pc.p.realm);
    w.setBlock(hit.block.x, hit.block.y, hit.block.z, empty, w.getMeta(hit.block.x, hit.block.y, hit.block.z));
    if (hit.id == full) pc.lv.fx->particles("firefly", vec3(hit.block) + vec3(0.5f, 0.6f, 0.5f), 10);   // they're free
    else pc.lv.fx->particles("poof", vec3(hit.block) + vec3(0.5f, 0.6f, 0.5f), 4);
    pc.lv.fx->material("glass", "place", vec3(hit.block) + vec3(0.5f));
    pc.swingArm();
    return true;
  });
}

} // namespace dq
