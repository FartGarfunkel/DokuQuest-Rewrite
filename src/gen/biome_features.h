// gen/biome_features.h — the land biomes' features (terrain version 15): the plants, flowers, grasses, water plants, sand and
// clay disks, mossy rocks, icebergs and the snow, placed as the data describes a biome's features: each biome's placed
// features (data/worldgen/placed_feature, configured_feature: the base set with the land biomes' changes), in the
// steps' order, each run from every chunk with its placement (count, rarity, in_square,
// heightmap, biome, block predicates, noise counts...) and its configured feature (random_patch, simple_block, the
// selectors, block_column, vegetation_patch, disk, kelp, seagrass, sea pickles, bamboo, vines, forest_rock, iceberg,
// blue_ice, freeze_top_layer, leveled patches, ponds and turnips).
// Trees are ours (gen/trees.cpp): their tree features are left out, as are the underground ones (ores, caves, dungeons,
// fossils, geodes: ours too). A feature reaches into the chunks round its own: a chunk runs its
// neighbours' features too and keeps what lands in it (their ground there read from its own edge).
// Coordinates: band y (the imported data's y is one less).
#pragma once
#include "common/common.h"
#include "common/fs.h"
#include "core/registry.h"
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dq {
struct GenBuf;
class LandBiomes;

// 2x2 cubes of stone / travertine / cobble on the grass (nature:large_pumpkin_feature with a rock). Pumpkins stay.
bool isClutterRockKey(std::string_view key);

class BiomeFeatures {
public:
  // dir: data/worldgen. False without the data.
  bool load(const fs::path& dir, const LandBiomes& biomes, u64 seed, bool groundCover = false);   // groundCover (terrain 26): the leaf litter under the woods (data/worldgen/ground_cover.json)
  bool ok() const { return loaded_; }
  // a chunk's features. biome: each column's biome at its ground (LandBiomes index, z*16+x); builtOn: true where a
  // structure will stand (no bamboo or cactus columns poking through its roofs)
  // underTrees (terrain version 17): the ground under our trees, not their crowns, is the features' ground
  void decorate(GenBuf& b, int cx, int cz, const u16* biome, const std::function<bool(int wx, int wz)>& builtOn, bool underTrees = false) const;

  struct State { BlockId id = 0xFFFF; u8 meta = 0; };
  struct Ctx;
  struct Rng;
private:
  struct IntP { u8 t = 0; int a = 0, b = 0, src = -1; float mean = 0, dev = 0; std::vector<std::pair<int, int>> wl; };
  struct Prov { u8 t = 0; std::vector<State> states; std::vector<int> weights; int total = 0; u64 seed = 0; double scale = 1, threshold = 0, high = 0;
                State def; std::vector<State> low, highS; int fallback = -1; std::vector<std::pair<int, int>> rules; };   // (rules: predicate, provider)
  struct Pred { u8 t = 0; int ox = 0, oy = 0, oz = 0; std::vector<u8> mask; State st; std::vector<int> kids; int dir = 0; };
  struct Mod { u8 t = 0; int ip = -1, ip2 = -1, hm = 0, pred = -1, pred2 = -1, a = 0, b = 0, dir = 0; float f = 0, g = 0, h = 0; };
  struct Placed { std::vector<Mod> mods; int conf = -1; bool ok = true, moves = false; };
  struct Conf { u8 t = 0; int a = -1, b = -1, c = -1, d = -1, e = -1, ia = -1, ib = -1, prov = -1, prov2 = -1, pred = -1; float f = 0, g = 0, h = 0;
                bool flag = false; State st, st2; std::vector<int> kids; std::vector<float> chances; std::vector<std::pair<int, int>> layers; std::vector<u8> mask; int reach = 0; };

  int parseInt(const json& j);
  int parseProv(const json& j);
  int parsePred(const json& j);
  int parsePlaced(const json& j);
  int parseConf(const json& j);
  int placedRef(const std::string& id);
  int confRef(const std::string& id);
  State state(const json& j);
  std::vector<u8> blockSet(const json& j);   // blocks, or a #tag, as a mask over our block ids
  void tagInto(const std::string& tag, std::vector<u8>& m, int depth = 0);
  int reachOf(int conf, int depth = 0) const;
  int intMax(int i) const;

  int sampleInt(int i, Rng& r) const;
  State provide(int p, Rng& r, int x, int y, int z, Ctx& c) const;
  bool test(int p, Ctx& c, int x, int y, int z) const;
  bool survives(BlockId id, Ctx& c, int x, int y, int z) const;
  bool placePlaced(int p, Ctx& c, Rng& r, int x, int y, int z) const;
  bool placeConf(int f, Ctx& c, Rng& r, int x, int y, int z) const;
  bool setState(Ctx& c, int x, int y, int z, State s) const;   // with a two-tall plant's top

  bool loaded_ = false;
  u64 seed_ = 0;
  const LandBiomes* bio_ = nullptr;
  std::vector<IntP> ints_;
  std::vector<Prov> provs_;
  std::vector<Pred> preds_;
  std::vector<Placed> placed_;
  std::vector<Conf> confs_;
  std::unordered_map<std::string, int> placedIds_, confIds_;
  std::unordered_map<std::string, std::vector<std::string>> tags_;
  fs::path dir_;
  static constexpr int STEPS = 11;
  std::vector<int> order_[STEPS];                // the steps' features in the data's order (first seen)
  std::vector<std::vector<u8>> has_[STEPS];      // per biome: which placed features its step lists (by placed index)
  std::vector<u8> dirt_, sand_, terracotta_, plantable_, iceish_;   // (masks the plants' rules use)
  BlockId water_ = 0, ice_ = 0, packed_ = 0, blueIce_ = 0, snow_ = 0, snowBlock_ = 0, grass_ = 0, snowyGrass_ = 0, podzol_ = 0, bamboo_ = 0, cactus_ = 0, cane_ = 0, vine_ = 0, seagrass_ = 0, kelp_ = 0, pickle_ = 0;
};

} // namespace dq
