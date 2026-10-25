// sim/redstone.h — redstone circuits (game/redstone.js), their placing and using (game/rsplace.js).
//
// Works like Minecraft Java at 20 game ticks a second (a redstone tick is two game ticks): power 0-15, dust
// loses one level per block, strong/weak power through solid blocks, torches, repeaters (delay, pulse
// stretching, locking), comparators (compare/subtract, reading containers), observers, lamps, copper bulbs,
// pistons and sticky pistons (slime/honey drag neighbours, PistonAnim slides the blocks), dispensers,
// droppers, hoppers, note blocks, bells, TNT, doors/trapdoors/gates, daylight detectors, targets,
// pressure plates, tripwire hooks and powered/detector/activator rails.
//
// One Redstone per World (realm), attached by the RedstoneLevel system; only the host runs circuits:
// nothing here runs on a joined client (lv.isRemote()) — guests see the results as block changes.
#pragma once
#include "world/dirs.h"
#include "sim/level.h"
#include "core/itemstack.h"
#include <unordered_set>
#include <deque>

namespace dq {

class Player;

namespace rs {
// circuit roles (core/blocks.js T_RS)
enum Role : u8 { NONE = 0, WIRE = 1, TORCH, TORCH_OFF, BLOCK, LEVER, BUTTON, PLATE, REPEATER, COMPARATOR, OBSERVER, LAMP, PISTON,
  DISPENSER, DROPPER, HOPPER, NOTE, TNT, DOOR, TRAPDOOR, GATE, DAYLIGHT, TARGET, PRAIL, DRAIL, BELL, BULB, HOOK };
// six-way directions (pistons, observers, dispensers): 0 down, 1 up, 2 north, 3 south, 4 west, 5 east (world/shapes.js)
constexpr int D6X[6] = {0, 0, 0, 0, -1, 1}, D6Y[6] = {-1, 1, 0, 0, 0, 0}, D6Z[6] = {0, 0, -1, 1, 0, 0};
constexpr int OPP6[6] = {1, 0, 3, 2, 5, 4};
constexpr int H2D6[4] = {2, 5, 3, 4};             // N E S W -> six-way
constexpr int D62H[6] = {-1, -1, 0, 2, 3, 1};     // six-way -> N E S W
// DIRX / DIRZ: world/dirs.h
constexpr int TORCH_SUP[5] = {0, 4, 5, 2, 3};    // torch meta -> six-way side of the block it hangs on
// rail shapes: 0 N-S, 1 E-W, 2-5 rising to E/W/N/S, 6-9 curves S-E, S-W, N-W, N-E; [shape][end] = {dx, dy, dz}
extern const int RAIL_EXITS[10][2][3];
enum RailKind : u8 { RAIL_NONE = 0, RAIL_NORMAL, RAIL_POWERED, RAIL_DETECTOR, RAIL_ACTIVATOR };

struct Tables {
  size_t nblocks = 0;
  u8 role[MAX_BLOCKS]{}, cond[MAX_BLOCKS]{}, ticker[MAX_BLOCKS]{}, rail[MAX_BLOCKS]{}, mech[MAX_BLOCKS]{};
  BlockId wire = 0, torch = 0, torchOff = 0, lamp = 0, lampOn = 0, bulb = 0, bulbLit = 0, piston = 0, sticky = 0, pistonExt = 0,
    stickyExt = 0, head = 0, moving = 0, slime = 0, honey = 0, tripwire = 0, water = 0, lava = 0, fire = 0, ore = 0, oreLit = 0,
    lily = 0, berryEmpty = 0;
};
const Tables& T();                  // built from the registry (rebuilt when blocks are added)
inline int leverSup(int m) { int k = m & 7; return k == 0 ? 0 : k == 1 ? 1 : H2D6[(k - 2) & 3]; }
inline int hookSup(int m) { return H2D6[m & 3]; }
inline int dirOf(float nx, float nz) { return std::fabs(nx) > std::fabs(nz) ? (nx > 0 ? 1 : 3) : (nz > 0 ? 2 : 0); }
int railShape(BlockId id, int m);
void wireConnects(const World& w, int x, int y, int z, int out[4]);   // per N/E/S/W: 0 none, 1 runs that way, 2 climbs
inline u64 posKey(int x, int y, int z) { return ((u64)((u32)(x + 0x2000000) & 0x3FFFFFF) << 38) | ((u64)((u32)y & 0xFFF) << 26) | (u64)((u32)(z + 0x2000000) & 0x3FFFFFF); }
float rnd();                        // 0..1 (simulation randomness)
std::string doorSound(const BlockDef& b, bool open);
// helpers shared by redstone, carts and machines
Entity* dropStack(Level& lv, int realm, const ItemStack& s, vec3 pos, vec3 vel = vec3(0), float pickupDelay = 0.5f);   // a dropped item entity
void dropBlockItems(Level& lv, int realm, int x, int y, int z, BlockId id);
bool boxFree(const World& w, vec3 lo, vec3 hi);
Player* playerNamed(Level& lv, const std::string& name);
} // namespace rs

// What a block (or a cart) holds that hoppers, droppers and comparators can reach.
struct Container {
  enum Kind : u8 { None, Box, Furnace, Composter, Machine } kind = None;
  int x = 0, y = 0, z = 0;
  json* items = nullptr;        // json array of ItemStack json / null
  Entity* cart = nullptr;       // a chest/hopper cart (or a chest boat)
  const json* table = nullptr;  // machines: input item -> [out, n]
  explicit operator bool() const { return kind != None; }
  int size() const { return items && items->is_array() ? (int)items->size() : 0; }
  ItemStack get(int i) const { return ItemStack::fromJson((*items)[i]); }
  void set(int i, const ItemStack& s) { (*items)[i] = s.empty() ? json(nullptr) : s.toJson(); }
};
int fillSignal(const json& items);   // how full a container looks to a comparator (0-15)

class Redstone : public WorldSystem {
public:
  Redstone(Level& lv, World& w);
  const char* name() const override { return "redstone"; }
  void tick(World&, double dt) override;     // runs step() 20 times a second
  void onBlockChanged(World&, int x, int y, int z, BlockId oldId, BlockId newId) override;
  void onChunkLoaded(World&, Chunk& c) override;
  void onChunkUnloading(World&, Chunk& c) override;

  Level& lv;
  World& w;
  i64 now = 0;                 // game ticks (20/s)
  double acc = 0;

  void step();                 // one game tick
  void queue(int x, int y, int z);
  void schedule(int x, int y, int z, int delay, const char* kind, bool want = false);
  void unschedule(int x, int y, int z) { sched.erase(rs::posKey(x, y, z)); }

  // power
  int emit(int x, int y, int z, BlockId id, int m, int d);
  int strong(int x, int y, int z, BlockId id, int m, int d);
  int blockPower(int x, int y, int z, bool strongOnly);
  int inputFrom(int x, int y, int z, int d);
  int powerAt(int x, int y, int z, int skip = -1);

  // containers (hoppers, droppers, comparators, carts, machines)
  Container inv(int x, int y, int z);
  bool insertOne(Container& c, int from, const ItemStack& s);   // one of s went in
  bool moveOne(Container& a, int to, Container& b, int from);
  void changedInv(Container& c);
  void daylightTick(int x, int y, int z, BlockId id, int m);
  void targetHit(int x, int y, int z, vec3 hit);                // an arrow hit a target block
  void primeTnt(int x, int y, int z, float fuse);
  void pistonPartBroken(int x, int y, int z, BlockId oldId);
  void dropAttached(int x, int y, int z);
  void finishAnims();          // blocks in the middle of a piston move settle (before saving)
  bool pistonGuard = false;    // set while pistons change blocks (pistonPartBroken must not react)

private:
  struct Sched { int x, y, z; i64 t; std::string kind; bool want; u64 seq, uid; };
  std::vector<ivec3> pending; std::unordered_set<u64> pendingSet;
  std::unordered_map<u64, Sched> sched;
  u64 seqN = 0, uidN = 0;
  std::unordered_map<u64, std::deque<i64>> burn;
  std::unordered_map<u64, i64> plateSeen;
  std::unordered_set<u64> netDone;
  std::unordered_map<u64, u32> anims;   // piston -> its PistonAnim entity
  std::unordered_map<ChunkKey, std::unordered_set<int>, ChunkKeyHash> tickers;   // colIdx of timer blocks per chunk
  struct EBox { vec3 lo, hi; int kind; };   // 0 mob, 1 item, 2 cart
  std::vector<EBox> ents; bool entsBuilt = false;

  void observe(int x, int y, int z);
  void update(int x, int y, int z);
  void fire(const Sched& s);
  bool supportPowered(int x, int y, int z);
  void dustArms(int x, int y, int z, bool arms[4]);
  void wireLinks(int x, int y, int z, std::vector<ivec3>& out);
  int dustInput(int x, int y, int z);
  void updateWire(int x, int y, int z);
  bool repeaterLocked(int x, int y, int z, int m);
  int sideInput(int x, int y, int z, int d);
  int comparatorValue(int x, int y, int z, int m);
  int measure(int x, int y, int z);
  bool railPowered(int x, int y, int z, BlockId id, int m);
  void noteTicker(int x, int y, int z);
  void scanTickers(Chunk& c);
  void runTickers();
  void buildEnts();
  int countIn(vec3 lo, vec3 hi, int onlyKind);
  void plateTick(int x, int y, int z, BlockId id, int m);
  void hookTick(int x, int y, int z, BlockId id, int m);
  void detectorTick(int x, int y, int z, BlockId id, int m);
  void queueAround(int x, int y, int z);
  void hopperTick(int x, int y, int z, BlockId id, int m);
  std::vector<int> insertSlots(Container& c, int from, const ItemStack& s);
  std::vector<int> extractSlots(Container& c, int to);
  bool moveSlot(Container& a, int slot, Container& b, int from);
  void dispense(int x, int y, int z);
  void spit(int x, int y, int z, int dir, const ItemStack& s);
  // pistons (sim/redstone_pistons.cpp)
  struct Plan { std::vector<std::array<int, 5>> move; std::vector<std::array<int, 4>> breaks; };   // move: x,y,z,id,meta
  void pistonMove(int x, int y, int z);
  bool gather(int sx, int sy, int sz, int mv, ivec3 piston, Plan& out);
  void extend(int x, int y, int z, BlockId id, int dir);
  void retract(int x, int y, int z, BlockId id, int dir);
  void carry(const Plan& plan, int mv, const std::function<void()>& then, std::vector<std::array<int, 9>> extra, u64 key);
  friend class RedstoneLevel;
};

// The level side: attaches a Redstone to every realm, finishes piston moves before saving.
class RedstoneLevel : public LevelSystem {
public:
  explicit RedstoneLevel(Level& lv);
  ~RedstoneLevel() override;
  const char* name() const override { return "redstone"; }
  void tick(Level& lv, double dt) override;
  void save(Level& lv) override;
  Redstone* of(int realm);
  Level& lv;
private:
  std::map<int, std::shared_ptr<Redstone>> per;
  int useHandler = 0, actHandler = 0;
};
Redstone* redstoneOf(Level& lv, int realm);   // nullptr on a joined client

// ---- placing and using (sim/rsplace.cpp): EventBus "itemUse" handler -----------------------------------------
// Handles: placing redstone parts, rails (they shape themselves), mech blocks (axis from the clicked face);
// using levers, buttons, repeaters (delay), comparators (subtract), daylight detectors (invert),
// hoppers/dispensers/droppers (emits "screenOpen" [player, "container", {pos:[x,y,z]}]), redstone ore glow;
// placing minecarts on rails and boats on water; using vehicles (ride, feed coal, chain carts, open chests).
bool rsItemUse(Level& lv, const json& args);   // false = consumed
bool rsFloorOK(const World& w, int x, int y, int z, bool sink);
// a walled town's gate lever (towns3.js): toggles the gate; on a joined client asks the host (netAction "gateLever")
bool gateLever(Level& lv, Player* p, World& w, int x, int y, int z);

void registerRedstone();   // level systems + entity factories (called from initCore)

} // namespace dq
