// world/dims.cpp — the world's layout: which dimension sits at which band of the column (world/dims.h)
#include "world/dims.h"

namespace dq {

WorldLayout makeLayout(int id, int underDepth) {
  WorldLayout L; L.id = id;
  for (int& d : L.dimAt) d = -1;
  for (int& p : L.posOf) p = -1;
  std::vector<int> order;   // bottom up
  if (id >= 2) {
    L.underDepth = clampv(underDepth, 1, 4);
    order = {DIM_END, DIM_NETHER};
    for (int k = L.underDepth; k >= 1; k--) order.push_back(DIM_UNDER1 + k - 1);   // the deepest first
    for (int d : {(int)DIM_OVERWORLD, (int)DIM_SKY1, (int)DIM_SKY2, (int)DIM_AETHER}) order.push_back(d);
  } else {
    L.id = 1; order = {DIM_END, DIM_NETHER, DIM_OVERWORLD, DIM_AETHER};
  }
  L.bands = (int)order.size();
  for (int b = 0; b < L.bands; b++) { L.dimAt[b] = order[b]; L.posOf[order[b]] = b; }
  return L;
}

void setWorldLayout(int id, int underDepth) {
  worldLayout() = makeLayout(id, underDepth);
  WORLD_H = worldLayout().bands * DIM_H;
}

} // namespace dq
