// sim/weapon_rack.cpp — batche.js (0.9.0.82) weapon racks: stand one on the floor or hang it on a wall (placed
// against the side of a block it becomes the wall rack), then rest up to three weapons, tools, bows or shields on
// it. The items live in the block entity {type:'rack', rack:[3]}, save with the chunk and spill out when it breaks.
#include "sim/weapon_rack.h"
#include "sim/gameplay.h"
#include "sim/controller.h"
#include "world/dirs.h"
#include "core/registry.h"
#include <algorithm>

namespace dq {

void registerWeaponRack() {
  // floor or wall, like torches
  hooks().place.push_back([](PlayerController& pc, const RayHit& hit, BlockId bid, Placement& out) -> int {
    BlockId rack = reg().blockId("weapon_rack"), wallRack = reg().blockId("wall_weapon_rack");
    if (!bid || bid != rack || !wallRack) return -1;
    World& w = pc.world();
    ivec3 t = pc.placeTarget(hit);
    out.x = t.x; out.y = t.y; out.z = t.z;
    if (hit.normal.y == 0) {
      int f = dirOf((float)-hit.normal.x, (float)-hit.normal.z);
      if (!reg().tSolid[w.getBlock(t.x + DIRX[f], t.y, t.z + DIRZ[f])]) return 0;
      out.meta = (u8)f;
      out.also.push_back({t.x, t.y, t.z, (int)wallRack, f});
      return 1;
    }
    if (!reg().tSolid[w.getBlock(t.x, t.y - 1, t.z)]) return 0;
    out.meta = (u8)lookDir(pc.p);
    return 1;
  });

  // rest what you hold on the slot nearest where you point; an empty hand takes the nearest item back
  hooks().useBlock.push_back([](PlayerController& pc, const RayHit& hit, ItemStack* held, const ItemDef* def) -> bool {
    int rk = (int)reg().block(hit.id).num("rack");
    if (rk <= 0) return false;
    bool hasHeld = held && !held->empty();
    if (pc.p.sneaking && hasHeld) return false;
    if (hasHeld && def && !def->block.empty()) return false;
    World& w = pc.world();
    int x = hit.block.x, y = hit.block.y, z = hit.block.z, f = w.getMeta(x, y, z) & 3;
    bool wall = rk == 2;
    json* e = w.blockEntity(x, y, z, true);
    if (!e) return false;
    if (!e->is_object() || e->value("type", "") != "rack" || !e->contains("rack") || !(*e)["rack"].is_array())
      *e = json{{"type", "rack"}, {"rack", json::array({nullptr, nullptr, nullptr})}};
    json& R = (*e)["rack"];
    while (R.size() < 3) R.push_back(nullptr);
    vec3 fr = pc.hitFrac(hit);
    int order[3] = {0, 1, 2}; float d[3];
    for (int k = 0; k < 3; k++) { vec3 o = vec3(rackSlotMat(f, wall, k)[3]); d[k] = glm::dot(o - fr, o - fr); }
    std::sort(order, order + 3, [&](int a, int b) { return d[a] < d[b]; });
    auto empty = [&](int k) { return R[k].is_null() || ItemStack::fromJson(R[k]).empty(); };
    if (hasHeld) {
      int k = -1; for (int i : order) if (empty(i)) { k = i; break; }
      if (k < 0) { pc.toastMsg("This rack is full"); return true; }
      ItemStack one = *held; one.n = 1; R[k] = one.toJson();
      if (--held->n <= 0) held->clear();
    } else {
      int k = -1; for (int i : order) if (!empty(i)) { k = i; break; }
      if (k < 0) return false;
      giveItem(pc.lv, pc.p, ItemStack::fromJson(R[k])); R[k] = nullptr;
    }
    w.blockEntityChanged(x, y, z);
    sfxAt(pc.lv, "click", vec3(x, y, z) + vec3(0.5f));
    pc.swingArm();
    return true;
  });
}

} // namespace dq
