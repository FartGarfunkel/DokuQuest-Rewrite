// sim/charcoal_pit.h — TerraFirmaCraft's charcoal pit: log piles (sim/piles.h) packed together and covered over (every face
// against another pile, or earth or stone, nothing that burns), lit with a firestarter; the fire spreads to every pile
// touching it and smoulders six in-game hours (TFC's eighteen, shortened). A pile with a face open to the air burns away.
// Each finished pile becomes a heap of charcoal (a ground-storage pile), a quarter to a half of its logs.
#pragma once
#include "sim/level.h"

namespace dq {

constexpr double CHARCOAL_HOURS = 6;
bool charcoalLight(Level& lv, World& w, int x, int y, int z);   // a spark on a log pile: it and the piles touching it
bool charcoalCovered(World& w, int x, int y, int z);            // every face against a pile or something that doesn't burn
void charcoalTick(Level& lv, double dt);                         // sim/content.cpp

} // namespace dq
