// gen/village_pieces.h — our villages' buildings: Better Village's houses, streets, town centres and street ends for
// plains, savanna, snowy, desert and taiga (data/villages/<style>/<kind>/*.nbt, index.json; tools/port/village_import.py
// brings them in). Read on first use, their Minecraft block states turned into ours (gen/village_pieces.cpp);
// gen/structures_jigsaw_village.cpp puts them together the way Minecraft's jigsaw does.
#pragma once
#include "core/registry.h"
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dq {

struct VPState { BlockId id = 0; std::array<u8, 4> meta{}; u8 kind = 0, banner = 0; BlockId rug = 0; std::string plant, items, etype; };   // rug: the carpet laid on it as a rug (world/rug.h)   // items: what it shows or keeps, "a,b*3,,c"; etype: pile, shelf, bookcase, frame, rack, cabinet   // meta per quarter turn; banner 1 on a wall, 2 standing
enum : u8 { VPK_BLOCK = 0, VPK_AIR = 1, VPK_CHEST = 2, VPK_BED_FOOT = 3, VPK_POT = 4, VPK_JOB = 5, VPK_BELL = 6, VPK_WARES = 7 };
struct VPBlock { u8 x, y, z; u16 s; };
// a jigsaw block: where pieces join. front 0..3 = north, east, south, west (4 up, 5 down: villager and animal markers)
struct VPJoint { int x = 0, y = 0, z = 0, front = 0; std::string name, target, pool; int fin = -1; };
struct VPPiece {
  std::string name, kind, style;          // "plains_small_house_1", "houses", "plains"
  int sx = 0, sy = 0, sz = 0, top = 0;     // size; top = the highest layer with anything in it
  int weight = 1;
  bool terrain = false;                    // streets and their ends follow the land; everything else stands rigid
  std::vector<VPState> states;
  std::vector<VPBlock> blocks;             // no air (Minecraft places none of a village piece's air either)
  std::vector<VPJoint> joints;
  std::string animals;                     // a pen's beasts ("sheep", "" = cows, pigs and chickens) from its markers
};
struct VillagePieces {
  std::map<std::string, std::vector<VPPiece>> pools;   // "plains/houses" -> its pieces
  const std::vector<VPPiece>* pool(const std::string& name) const;   // "minecraft:village/plains/houses" or "plains/houses"
};

const VillagePieces* villagePieces();   // nullptr: data/villages is missing (the classic villages are built)
// a piece's block at a quarter turn r (clockwise seen from above, like Minecraft's rotation): its offset in the turned box
inline std::array<int, 2> vpTurn(const VPPiece& p, int r, int x, int z) {
  switch (r & 3) { case 1: return {p.sz - 1 - z, x}; case 2: return {p.sx - 1 - x, p.sz - 1 - z}; case 3: return {z, p.sx - 1 - x}; default: return {x, z}; }
}

} // namespace dq
