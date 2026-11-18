// gen/huge_mushroom.h — the huge mushroom of the mushroom islands (gen/decorate.cpp), shared by world generation and
// bone meal on a small mushroom (sim/world_life.cpp). A stem `hgt` tall from (x, y, z) up; a red one wears a dome (a ring
// hanging down round a top), a brown one a flat plate. `set` is called for each block, stem first.
#pragma once
#include "world/chunk.h"
#include <functional>
#include <vector>

namespace dq {

void buildHugeMushroom(const std::function<void(int, int, int, BlockId)>& set, int x, int y, int z, bool red, int hgt,
                       BlockId stem, BlockId redCap, BlockId brownCap);
// the cells it fills, as offsets from the stem's foot (for a room check before growing one)
void hugeMushroomCells(bool red, int hgt, std::vector<ivec3>& out);

} // namespace dq
