// sim/trommel.h — Better than Adventure 8.0's Trommel (TileEntityTrommel): four input slots and a fuel slot; while it
// burns furnace fuel it sifts one item every 2.5 s, taking the input slots in turn, and what it finds (BTA's weighted
// table, data/trommel.json from tools/port/trommel.py) pops out of its left side — into a chest standing there, else
// flung onto the ground. Now and then (1 in 4000) a small slime tumbles out instead. Fuel lasts twice as long as in a
// furnace (BTA: coal sifts 32). The block is a furnace kind ("furnace": "trommel"), so it opens, lights and keeps its
// block entity ({type: furnace, items[5], burn, burnMax, cook, next}) like one; tickFurnaces hands it to tickTrommel.
#pragma once
#include "common/common.h"
#include "common/fs.h"   // json

namespace dq {

class Level; class World;
bool trommelSifts(const std::string& key);    // can it go in an input slot
constexpr float TROMMEL_POP = 2.5f;          // seconds per item (BTA: 50 ticks)
// one step of a trommel at pos (its block's meta for the facing); returns true if it changed
bool tickTrommel(Level& lv, World& w, int realm, json& e, double dt, ivec3 pos, u8 meta);
// what an input can give, for the screen: [(chance 0..1, item)]
std::vector<std::pair<float, std::string>> trommelYields(const std::string& key);

} // namespace dq
