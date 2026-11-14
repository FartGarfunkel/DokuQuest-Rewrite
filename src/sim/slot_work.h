// sim/slot_work.h — the trommel and the machines (millstone, sawmill, crusher) worked by hand: what's in the hand put
// into its hopper / mouth / firebox, what's done taken back out (the scene: ui/cinematic_slots.cpp). The same block
// entities as their screens (sim/trommel.h items[5]: four inputs and the fuel; sim/machines.h items[2]: in and out).
#pragma once
#include "common/common.h"
#include "core/itemstack.h"
#include <vector>

namespace dq {
class Level; class Player; class World;

enum class SlotRig { None, Trommel, Machine };
SlotRig slotRigAt(World& w, ivec3 at);
std::vector<ItemStack> slotItems(World& w, ivec3 at);                                // the entity's items, as stored
// spot: the trommel 0 its hopper (the first of its four inputs that takes it), 1 its firebox; a machine 0 its mouth
bool slotPut(Level& lv, Player& p, World& w, ivec3 at, int spot, ItemStack& held);
// spot: the trommel 0 the inputs (back to you, the last first), 1 the fuel; a machine 0 the input, 1 the output
bool slotTake(Level& lv, Player& p, World& w, ivec3 at, int spot);
float slotProgress(World& w, ivec3 at);   // 0..1: the trommel's sift, a machine's grinding
float slotFuel(World& w, ivec3 at);       // 0..1: the trommel's fire (a machine: 1 when turned, else 0)

} // namespace dq
