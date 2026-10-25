// sim/maps.h — paper maps (game/maps.js, 0.8.6.2).
//
// Eight paper round a compass make an empty map. Use it and it becomes a map of the 128 x 128 blocks where you
// stand, lined up on a grid so the maps either side fit edge to edge. It fills in as you walk about holding it
// (either hand). Maps are saved with the world as records of their own (`<world>/maps/<id>.json`, the original's
// 'map~id' record: palette + RLE + base64), stacks carry the map's id in tag "map".
// The pictures (colours from the tiles), the map in your hands and the map screen are in render/map_render.* and
// ui/map_screen.cpp.
#pragma once
#include "sim/content.h"
#include <set>

namespace dq {

constexpr int MAP_N = 128, MAP_PIX = MAP_N * MAP_N, MAP_MAX_SCALE = 4, MAP_R = 64;
extern const char* const MAP_PIN_COLS[6];

struct PaperMap {
  std::string id;
  int dim = 0;                 // the original's dimension number (JS DIM: 0 overworld, 1 depths, 2 skylands, 3 pocket, 4 realm, 5 end)
  int x0 = 0, z0 = 0, scale = 0;
  std::vector<u16> ids;        // the block seen from above (runtime ids)
  std::vector<u8> h;           // its band-local height + 1 (0: not explored yet)
  std::vector<u8> cl;          // climate (temperature << 4 | humidity), or water depth for water
  u32 ver = 1;
  PaperMap() : ids(MAP_PIX, 0), h(MAP_PIX, 0), cl(MAP_PIX, 0) {}
  int span() const { return MAP_N << scale; }
  bool has(double x, double z) const { return x >= x0 && z >= z0 && x < x0 + span() && z < z0 + span(); }
  bool setPixel(int i, u16 id, u8 hh, u8 c);
  float explored() const;
};

int mapAlign(int v, int scale);
json encodeMap(const PaperMap& m);                  // the save record
std::unique_ptr<PaperMap> decodeMap(const json& rec);
// RLE as world/chunk.js rleEncode/rleDecode (identity order: MAP_ORDER)
std::vector<u8> mapRleEncode(const u16* src, size_t n, bool wide);
std::vector<u8> mapRleEncode(const u8* src, size_t n);
void mapRleDecode(const std::vector<u8>& bytes, u16* dst, size_t n, bool wide);
void mapRleDecode(const std::vector<u8>& bytes, u8* dst, size_t n);

// The maps of one open world (a LevelSystem; lv.system<MapStore>()).
class MapStore : public LevelSystem {
public:
  explicit MapStore(Level& lv) : lv(lv) {}
  const char* name() const override { return "maps"; }
  void save(Level& l) override;
  PaperMap* get(const std::string& id);                                  // mapGet (loads the record)
  PaperMap* create(const std::string& id, int dim, double x, double z, int scale);
  PaperMap* zoomed(const PaperMap& src);                                // mapZoomed: twice the area
  PaperMap* forStack(Player& p, ItemStack& s);                          // mapForStack (one with no map yet becomes a map of here)
  std::vector<PaperMap*> sameScale(const PaperMap& m);                  // every map of this place and scale (the map screen)
  const std::map<std::string, std::unique_ptr<PaperMap>>& all() const { return maps; }
  std::set<std::string> dirty;
  std::unordered_map<u32, u32> sweep;                                    // player id -> explore sweep counter
private:
  Level& lv;
  std::map<std::string, std::unique_ptr<PaperMap>> maps;
  std::set<std::string> missing;
};
MapStore& mapStore(Level& lv);

// exploring: a few columns of the map under you each tick
void mapExplore(Level& lv, Player& p, PaperMap& m, int columns = 6);
// the world and band a map draws (realm, C++ dim) from its original dimension number
void mapWorldOf(int jsDim, int& realm, int& dim);

// markers, in map pixels (0..128): 0 you, 1 someone here, 2 someone over the network, 3 off the edge, 4 home,
// 5 grave, 6..11 pins, 12 village, 13 a place/dungeon/waystone/house/job, 14 the rift, 15 a cleared place
struct MapMarker { int cell = 0; float u = 0, v = 0, rot = 0; bool small = false, pin = false; std::string label; };
std::vector<MapMarker> mapMarkers(Level& lv, const PaperMap& m, const Player* viewer);
// other areas add their own marks (quests' jobs, raids...): called for each map drawn
using MapMarkerSource = std::function<void(Level&, const PaperMap&, const Player*, std::vector<MapMarker>&)>;
void addMapMarkerSource(MapMarkerSource f);

// pins (LevelInfo.data["mapPins"]: [{d, x, z, c}]): click near one (within `near` blocks) clears it, else a new one
std::string mapTogglePin(Level& lv, Player& p, int dim, float x, float z, float near);

// using maps; the M key: the slot of the map to look at (-1 none, -2 the off hand)
int mapBestSlot(Level& lv, Player& p);
ItemStack* mapStackAt(Player& p, int slot);
// crafting (maps.js matchGrid): a map and empty maps -> copies; a map ringed with 8 paper (3x3) -> zoomed out.
// take = the result is being taken (a zoomed map is made then). Returns false if this isn't a map recipe.
bool mapCraft(Level& lv, const std::vector<ItemStack>& slots, int W, ItemStack& out, bool take);
// the tooltip lines (itemInfoHTML)
std::vector<std::string> mapItemInfo(Level& lv, const ItemStack& s);
inline bool isMapStack(const ItemStack& s) { return !s.empty() && s.id == "filled_map"; }
inline std::string stackMapId(const ItemStack& s) { return s.tag.is_object() && s.tag.contains("map") && s.tag["map"].is_string() ? s.tag["map"].get<std::string>() : ""; }

} // namespace dq
