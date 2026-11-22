// gen/proc_rooms.h — shared furnished rooms for large structures (crypts, mineshafts, fortresses, strongholds).
// Classic one-room dungeons do not use this.
#pragma once
#include "gen/structures_common.h"
#include <functional>

namespace dq {

struct ProcRoom {
  std::string kind;
  int x = 0, y = 0, z = 0;
  int rx = 3, rz = 3, H = 4;
  u32 seed = 0;
  std::string wood = "oak";
  std::string light = "old_torch";
  std::string hangLight = "lantern";
  std::string pillar = "stone_bricks";
  std::string slab = "stone_brick_slab";
  std::string stairs = "stone_brick_stairs";
  bool webs = false;
  float density = 0.65f;   // 0.35 sparse, 0.65 lived-in, 0.9 packed
};

inline constexpr const char* PROC_ROOM_KINDS[] = {
  "entry", "hall", "loot", "spawner", "library", "boss", "barracks", "dining", "chapel",
  "kitchen", "armoury", "cells", "workshop", "crypt", "well", "gallery", "shrine", "store",
  "sleeping", "refectory", "scriptorium", "nave", "vault", "prison", "office", "forge"
};
inline constexpr int PROC_ROOM_KIND_N = 26;
inline constexpr int DUNGEON_GRID_MIN = 6, DUNGEON_GRID_MAX = 8;
inline constexpr int DUNGEON_CELL_MIN = 9, DUNGEON_CELL_MAX = 13;
inline constexpr int DUNGEON_FLOORS_MIN = 2, DUNGEON_FLOORS_MAX = 4;
inline constexpr int DUNGEON_ROOMS_MIN = 12, DUNGEON_ROOMS_MAX = 24;

inline constexpr const char* PROC_FURNITURE[] = {
  "oak_chair", "oak_table", "oak_bench", "oak_stool", "oak_shelf", "oak_standing_shelf", "oak_bookcase",
  "brazier", "old_torch", "torch", "soul_torch", "lantern", "soul_lantern", "candle",
  "barrel", "lectern", "armor_stand", "weapon_rack", "wall_weapon_rack", "campfire",
  "bookshelf", "decorated_pot", "cauldron", "anvil", "bed", "iron_bars", "chain", "grave",
  "oak_desk", "oak_coffin", "altar", "sconce"
};
inline constexpr int PROC_FURNITURE_N = 32;

using ProcChestFn = std::function<void(int x, int y, int z, const char* lootKind, bool mimic)>;

// fill a carved room with furniture, lights and clutter. chest may be empty (no chests).
void furnishProcRoom(SP& P, const ProcRoom& r, const ProcChestFn& chest = {});

// pick a room kind for a role: "entry", "boss", "spawner", or "" for the rest
std::string pickProcKind(Mulberry32& rnd, const std::string& role, int salt = 0);

// a wood that exists as furniture, falling back to oak
std::string procWood(const std::string& want);

} // namespace dq
