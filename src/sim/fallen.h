// sim/fallen.h — the fallen adventurers (our own, after Realm RPG: Fallen Adventurers' idea): an adventurer's bones where
// they fell (remains_traveller / desert / snowy / jungle / swamp / miner, tools/port/polish_data.py), a few of their
// things on them. What they carry is rolled the first time they're opened (their kind and where they lie decide it,
// the same for everyone); now and then they're a trap, and opening them raises what killed them.
#pragma once
#include "sim/level.h"

namespace dq {
class Player;

// fills a remains' container record (sim/build.cpp containerEntity, the first time it's made)
void fallenFill(World& w, int x, int y, int z, json& be);
// opening it (sim/controller.cpp, before the container screen): a trap springs (the host's game only)
void fallenOpened(Level& lv, Player& p, World& w, int x, int y, int z);

} // namespace dq
