// sim/machines.h — windmills, waterwheels, axles and the machines they turn (game/mech.js), conveyor belts,
// the crusher and the harvester (game/machines.js).
//
// A windmill (high up, sails in open air) or a waterwheel (in flowing water) turns its axle; axles carry it
// along their length into millstones, sawmills, crushers, harvesters and belts. Everything joined shares
// the power: more sources turn faster, more machines share it out. Machines keep {type:"machine",
// items:[input, output], progress} block entities (hoppers feed them through sim/redstone Container).
#pragma once
#include "sim/level.h"

namespace dq {

class Player;

struct MechNode {
  ivec3 p{0};
  BlockId id = 0;
  std::string kind;     // axle windmill waterwheel millstone sawmill crusher harvester belt
  int axis = 0;         // 0 up, 1 east-west, 2 north-south (meta & 3)
  int comp = -1;
  float power = 0;
  bool blocked = false;
};
struct MechComp { std::vector<u64> nodes, sources, machines, belts; float speed = 0, angle = 0; };

class Mechanics : public WorldSystem {
public:
  Mechanics(Level& lv, World& w) : lv(lv), w(w) {}
  const char* name() const override { return "mechanics"; }
  void onBlockChanged(World&, int x, int y, int z, BlockId oldId, BlockId newId) override;
  void onChunkLoaded(World&, Chunk& c) override;
  void onChunkUnloading(World&, Chunk& c) override;
  void update(double dt);                  // called by MechLevel every tick (also on clients, for the turning)
  void rebuild();
  struct Source { float power; bool blocked; };
  Source sourcePower(const MechNode& n);
  float speedAt(int x, int y, int z) const;
  float angleOf(const MechNode& n) const { return n.comp >= 0 && n.comp < (int)comps.size() ? comps[n.comp].angle : 0.f; }
  const std::unordered_map<u64, MechNode>& nodes() const { return nodeMap; }
  Level& lv;
  World& w;
private:
  std::unordered_map<u64, MechNode> nodeMap;
  std::unordered_map<ChunkKey, std::vector<u64>, ChunkKeyHash> perChunk;
  std::vector<MechComp> comps;
  bool dirty = true;
  float t = 0, creakT = 0;
  std::unordered_map<u64, float> ejectT;
  void addNode(int x, int y, int z, BlockId id, int meta);
  void work(MechNode& m, float amount);
  void crusherWork(MechNode& m, float amount);
  void harvesterWork(MechNode& m, float amount);
  void beltTick(double dt);
  json* machineEntity(const MechNode& m);
};

class MechLevel : public LevelSystem {
public:
  explicit MechLevel(Level& lv) : lv(lv) {}
  const char* name() const override { return "mechanics"; }
  void tick(Level& lv, double dt) override;
  Mechanics* of(int realm);
  Level& lv;
private:
  std::map<int, std::shared_ptr<Mechanics>> per;
};
Mechanics* mechanicsOf(Level& lv, int realm);

// tables (data/machines.json): input item -> [output, n]
const json* machineTable(const std::string& mechKind);   // millstone, sawmill, crusher (nullptr otherwise)
float compostChance(const std::string& itemKey);
// A mech block was placed (build.js -> Mechanics.placed): its axis/direction from the clicked face or the
// player's facing, its block entity, the windmill's advice.
void mechPlaced(Level& lv, Player* p, World& w, int x, int y, int z, BlockId id, int face);

} // namespace dq
