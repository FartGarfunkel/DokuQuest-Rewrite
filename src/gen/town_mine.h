// gen/town_mine.h — the town mine's plan, shared by its builder (gen/structures_townbld.cpp bMine) and the miner who
// works it (sim/villager_chores.cpp). Building-local coordinates: x across the headframe's plot (0..6), z in from the
// street, y from the plot's floor. Under the headframe a drift of steps runs down to a timbered chamber; from it three
// galleries run out, mineshaft-fashion (fence posts and a plank cap every few blocks, a rail down the middle), each to an
// ore face at its end, with a short side cut halfway along.
#pragma once

namespace dq::townmine {

constexpr int FLOOR = -12;                                   // the chamber's and the galleries' floor (the drift's last step)
constexpr int HUB_X0 = -1, HUB_X1 = 7, HUB_Z0 = 13, HUB_Z1 = 19;   // the chamber at the drift's foot
struct Gallery { int x, z, dx, dz, len; };                   // its middle line's first cell, the way it runs, its length
constexpr Gallery GAL[3] = {{3, 20, 0, 1, 18}, {-2, 16, -1, 0, 16}, {8, 16, 1, 0, 16}};
// everything it digs, walls and all (the town's bounds and the chunks that build it take this in)
constexpr int REACH_X0 = -20, REACH_X1 = 26, REACH_Z0 = -1, REACH_Z1 = 40;

// a cell of gallery g: `step` along it, `side` across (-1..1)
inline void galleryCell(int g, int step, int side, int& lx, int& lz) {
  const Gallery& G = GAL[g];
  lx = G.x + G.dx * step + (G.dz ? side : 0);
  lz = G.z + G.dz * step + (G.dx ? side : 0);
}

} // namespace dq::townmine
