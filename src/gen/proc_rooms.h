// gen/proc_rooms.h — shared furnished rooms for large structures (crypts, mineshafts, fortresses, strongholds).
// Classic one-room dungeons do not use this.
#pragma once
#include "gen/structures_common.h"
#include <functional>

namespace dq {

struct ProcLayout { int yaw = 0, pattern = 0, pal = 0, clutter = 1; };

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
  int yaw = 0;       // 0..3, 90° CW of the whole layout
  int pattern = 0;   // 0..7, furniture positions in local space
  int pal = 0;       // 0..5, see palette()
  int clutter = 1;   // 0 sparse, 1 lived-in, 2 packed, 3 ruined
};

struct ProcPal {
  const char* wall; const char* floor; const char* pillar; const char* slab;
  const char* stairs; const char* light; const char* hang; const char* wood;
};

enum ProcPat : int {
  PL_LONG    = 0,  // long table / pew / bench run along local +X
  PL_SHELVES = 1,  // two facing walls (local N+S) lined
  PL_DAIS_N  = 2,  // focal piece on local north
  PL_DAIS_S  = 3,  // focal piece on local south
  PL_BEDS_NS = 4,  // pairs on E/W walls, aisle N-S
  PL_BEDS_EW = 5,  // pairs on N/S walls, aisle E-W
  PL_L       = 6,  // L: local south + west
  PL_PACK    = 7   // all four walls / extra interior
};

inline constexpr int PROC_PATTERN_N = 8, PROC_PAL_N = 6, PROC_CLUTTER_N = 4, PROC_YAW_N = 4;

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

const ProcPal& palette(int pal);
void applyPalette(ProcRoom& r);
int rotDir(int dir, int yaw);
void rotOffset(int lx, int lz, int yaw, int& ox, int& oz);
void rotCell(const ProcRoom& r, int lx, int lz, int& x, int& z);
float clutterDensity(int clutter);
int pickDungeonPal(int theme, Mulberry32& rnd);
ProcLayout pickProcLayout(Mulberry32& rnd, int dungeonPal, int dungeonClutter);
void applyProcLayout(ProcRoom& r, const ProcLayout& L);

// one furnished room from a seed: yaw, pattern and clutter from the seed, palette chosen by the caller
void furnishPlaced(SP& P, int x, int y, int z, int rx, int rz, int H,
                   const std::string& kind, u32 seed, int pal, int clutter = 1,
                   const ProcChestFn& chest = {});

} // namespace dq
