// gen/structures_town.h — villages, towns and cities (game/villages.js + towns.js + villageflat.js + towns3.js gates +
// townlife.js cloth), shared by structures_villages.cpp (the plan, the square, streets, walls) and
// structures_townbld.cpp (the buildings: villages.js vStamp + towns.js vHouse and TOWN_VT).
// Every new C++ world is the original's terrain version 5: towns.js plans, level ground (villageflat.js).
#pragma once
#include "gen/structures_common.h"
#include <array>

namespace dq {

// a building style (VSTYLE after towns.js filled in its timber/plaster/roof choices)
struct VStyle {
  std::string key, planks, log, beam, base, floor, stairs, slab, fence, gate, door, trapdoor, wood, stone, stoneSlab, stoneStairs, lampPost,
      roof, rim, plaster, frame, beamH, base2, base3, leaves, fwood;
  std::vector<std::string> path, bed, awning, awning2, timber, banner;
  std::vector<std::array<std::string, 2>> roofs;
  std::array<std::string, 2> thatch;
  bool flat = false;
  int baseStyle = 0;   // the base style it's a kind of (VS_PLAINS..VS_DESERT): its ground, its stone, its golem
};
// 0 plains, 1 savanna, 2 taiga, 3 snowy, 4 desert, then the village pieces' own styles (data/villages/styles.json,
// tools/port/village_builder.py: beach, jungle, swamp, mesa, igloo, christmas, halloween...), each a kind of one of the five
const VStyle& vstyle(int style);
enum { VS_PLAINS, VS_SAVANNA, VS_TAIGA, VS_SNOWY, VS_DESERT };
int vstyleCount();
int vstyleIndex(const std::string& key);   // -1: no such style
inline int vbase(int style) { return vstyle(style).baseStyle; }
enum { TIER_VILLAGE, TIER_TOWN, TIER_CITY };

// a seat for the villagers' evenings: [x, y, z, facing, [x, z] to walk up to, what they hold]
struct VSeat { int x, y, z, f; bool walk = false; int ax = 0, az = 0; std::string held; };
struct VSpot { int x, y, z, f; };                  // a bed ({at, f}) or a workplace ({at, face})

struct Bld {
  std::string type;
  int r = 0, ox = 0, oz = 0, W = 0, D = 0, H = 0, y = 0, yRaw = 0, x0 = 0, z0 = 0, x1 = 0, z1 = 0, tier = 0, roofI = 0, bedCol = 0;
  u32 seed = 0;
  std::string job, station, crop, crop2, animals;
  std::vector<VSpot> beds;
  bool hasWork = false, hasElder = false, hasMid = false;
  VSpot work{}, elderAt{}, mid{};
  std::vector<std::pair<VSpot, std::string>> tasks;   // the trade's round: where it works each kind of station (jigsaw towns)
  int planV = 10;   // its town's plan version (the town mine is built to it)
  std::vector<VSeat> seats;
};
// a street; the main ones (roads) also say where the wall's gates are: across the town's centre lines in the classic
// towns, at (ox | oz) on its side in the jigsaw ones (gy: the gate's ground, -1 the town's level). gate false: a jigsaw
// town's street (its cells, for the villagers' walks), no gate.
struct Street { int d = 0, L = 0, hw = 0, ox = 0, oz = 0, t0 = 0, gy = -1; bool side = false, gate = true; std::vector<std::array<int, 3>> cells; };
struct Stall { int x, z, f; std::string wares; bool deco; };
struct TownWall { std::string kind; int x0, z0, x1, z1, h; };
struct WashLine { int x, z, ax, az, y, v; };
struct Bunting { int x0, x1, z, y; };
struct Town {
  std::string id, name;
  int style = 0, tier = 0, P = 0, hw = 0, fr = 0, x = 0, z = 0, y = 0;
  int planV = 10;   // the terrain version's village plan (11: gen/generator.h's fixes); a world keeps the one it was made with
  u32 seed = 0;
  std::vector<Street> roads, streets;
  std::vector<Bld> blds;
  std::vector<std::array<int, 2>> lamps, trees, banners;
  std::vector<std::array<int, 3>> decor;
  std::array<int, 3> board{}, bell{};
  std::vector<Stall> stalls;
  std::vector<VSeat> seats;
  bool hasWall = false; TownWall wall{};
  // townlife.js
  std::vector<WashLine> lines; std::vector<std::array<int, 4>> signs; std::vector<Bunting> bunting;
  // villageflat.js: the level ground
  std::vector<std::array<int, 4>> flatRects; std::array<int, 4> fb{};
  int wind = 1;
};

// the buildings (structures_townbld.cpp)
int townTemplateSize(const std::string& type, int& W, int& D, int& H);   // false (0) = no such building
void townBuildingPlan(Bld& b, Mulberry32& rnd, const VStyle& st);        // TOWN_VT[type].plan
void townBuildingBuild(SP& P, const Town& v, const Bld& b);              // vStamp + TOWN_VT[type].build, this chunk
const std::vector<std::string>& townDecor(const std::string& kind);
bool softBlock(BlockId id);    // villages.js soft(): air, not solid, liquid, a plant, leaves, logs, snow layers
// a town plan's main streets' ends, just past where they stop: {x, z, direction} (structures_roads.cpp runs roads from them)
std::vector<std::array<int, 3>> townStreetEnds(const StructurePlan& S);
int townStyleOf(const StructurePlan& S);

// a village of our pieces (data/villages, tools/port/village_builder.py) put together jigsaw-fashion (gen/structures_jigsaw_village.cpp, gen/village_pieces.h)
struct JigsawTown;
struct VillagePieces;
bool jigsawTownPlan(const TerrainQuery& t, const VillagePieces& pack, Town& v, Mulberry32& rnd, std::shared_ptr<JigsawTown>& out);
void jigsawTownBuild(SP& P, const Town& v, const JigsawTown& J);
bool jigsawTownFlatten(const JigsawTown& J, int x, int z, double& weight, int& targetY);
bool jigsawTownTreeOff(const JigsawTown& J, int x, int z);
void jigsawTownBounds(const JigsawTown& J, int& x0, int& z0, int& x1, int& z1);
const std::vector<std::array<int, 3>>& jigsawTownEnds(const JigsawTown& J);

// villages.js vRot / vWorld
inline std::pair<int, int> vRot(int r, int x, int z) { switch (r & 3) { case 0: return {x, z}; case 1: return {-z, x}; case 2: return {-x, -z}; default: return {z, -x}; } }
inline int vDirOf(int dx, int dz) { return dz < 0 ? 0 : dx > 0 ? 1 : dz > 0 ? 2 : 3; }
inline std::array<int, 3> vWorld(const Bld& b, int lx, int ly, int lz) { auto [a, c] = vRot(b.r, lx, lz); return {b.ox + a, b.y + ly, b.oz + c}; }

} // namespace dq
