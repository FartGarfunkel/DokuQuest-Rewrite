// sim/controller.h — one person playing (port of game/local.js LocalPlayer's rules + game/build.js,
// the player side of game/blocks2.js, blockfx.js flint/bone meal, furniture.js sitting and shelves,
// furnish.js sneak-placing, game/itemfix.js eating, fairplay.js food over time).
//
// A PlayerController drives a Player each frame from a PlayerInput: movement, breath, regen, fire,
// falls, the crosshair target, mining (speeds, tool tiers, drops, XP, tree felling), placing every
// block kind, using blocks and items, eating, attacking, Carry On, ground storage, beds, seats,
// buckets, dropping, pick block, creative flight. It runs in dqcore, so the dedicated server can
// drive remote players with the same code. Screens are asked for through the EventBus
// ("screenOpen" [player, kind, opts]) and the UI calls back into the controller/inventory.
#pragma once
#include "sim/gameplay.h"
#include "sim/combat.h"

namespace dq {

struct Mining { bool active = false; int x = 0, y = 0, z = 0; BlockId id = 0; float progress = 0, time = 0, pt = 0; };
struct Crack { int x, y, z, stage; };

class PlayerController {
public:
  PlayerController(Level& lv, Player& p);
  Level& lv;
  Player& p;
  World& world() { return lv.world(p.realm); }

  void update(float dt, const PlayerInput& in);   // LocalPlayer.update (call every frame the player is in the world)
  void respawn();                                 // from the death screen

  // the crosshair
  RayHit target;                 // block under the crosshair (hit=false: none)
  u32 targetEnt = 0;             // creature/vehicle under the crosshair
  vec3 targetPoint{0};
  bool aimOverride = false; glm::dvec3 aimOrigin{0}; vec3 aimDir{0};   // over-the-shoulder camera: aim from the camera
  Mining mining;
  int placeMode = 0;             // 0 normal, 1 vertical slabs / upside-down stairs and trapdoors (R)
  float breakCd = 0, useRepeat = 0, bowPull = 0, landT = 0, shakeAmt = 0;
  bool useArmed = false, jumpWas = false, sneakWas = false, fightHold = false;
  double t = 0;                  // this controller's clock (the original's g.time)
  double lastJump = -10, interactAt = -10, carryHint = -10;
  bool ledgeTip = false, chewTip = false;
  float fallDistSeen = 0;        // the fall we last wrote to Entity::fallDist
  json we = json::object();      // WorldEdit: pos1, pos2, clip, undo, redo
  std::vector<std::string> chatHist;

  // ---- rules (local.js) ----
  void updateTarget();
  void interact(float dt, const PlayerInput& in);
  void use(bool repeat, const PlayerInput& in, bool fromOff = false);
  void useOffhand(bool repeat, const PlayerInput& in);
  float breakTime(BlockId id, const RayHit* hit);
  bool canHarvest(BlockId id);
  void breakBlock(int x, int y, int z);
  void swingArm();
  int targetQuarter() const;
  vec3 hitFrac(const RayHit& hit) const;
  ivec3 placeTarget(const RayHit& hit) const;
  bool blockedByBody(int x, int y, int z);
  void pickBlock(const RayHit& hit);
  void dropHeld(bool all);
  void throwStack(ItemStack s);
  void breathe(float dt);
  void hurt(float amount, const DamageSource& src) { damagePlayer(lv, p, amount, src); }
  void toastMsg(const std::string& m, const std::string& kind = "") { toast(lv, p, m, kind); }
  void weClick(int which);

  // ---- building (build.js + blocks2.js + furniture.js) ----
  bool placement(const RayHit& hit, BlockId bid, Placement& out);    // false = refused
  void placeBlock(const RayHit& hit, const ItemDef& def, ItemStack& held);
  bool useBlockAt(const RayHit& hit, ItemStack* held, const ItemDef* def);
  bool supportOK(BlockId bid, int x, int y, int z, u8 meta);
  bool plantItem(const ItemDef& def, const RayHit& hit, ItemStack& held);
  void useBonemeal(const RayHit& hit, ItemStack& held);
  void useFlint(const RayHit* hit, ItemStack& held);
  void useBucket(const ItemDef& def, ItemStack& held);
  bool bucketWaterlog(const ItemDef& def, ItemStack& held);
  void afterBreak(int x, int y, int z, BlockId id, u8 meta);
  void sleep(int x, int y, int z, u8 meta);
  bool sitOn(int x, int y, int z);
  void dismount();
  bool putDown(const RayHit& hit, ItemStack& held);
  void usePile(const RayHit& hit, ItemStack* held);
  void pickUpBlock(int x, int y, int z);
  void placeCarried();
  void editSign(int x, int y, int z);
  bool eatFood(ItemStack& held, const ItemDef& def, bool repeat);
  void heldBecomes(ItemStack s);   // replace the held stack (bucket emptied, bowl left...)
  void consumeHeld(int n = 1);     // creative keeps it

private:
  void scanTree(int x, int y, int z, std::vector<std::array<int, 4>>& logs, std::vector<std::array<int, 4>>& leaves, bool& ok);
  void fellTree(int x, int y, int z, const std::vector<std::array<int, 4>>& logs, const std::vector<std::array<int, 4>>& leaves);
  void updateHeldTools(float dt, const PlayerInput& in);
  void tickSurvival(float dt, const MoveInfo& mv);
};

// Blocks left half-broken keep their cracks for a while and slowly mend (blockfx.js).
void rememberDamage(Level& lv, const Mining& m);
float damageFor(Level& lv, int x, int y, int z, BlockId id);
void forgetDamage(Level& lv, int x, int y, int z);
std::vector<Crack> damageCracks(Level& lv, World& w, vec3 cam, const Mining* current);   // for the renderer

// sitting: where can you sit, how high (pixels) and facing which way (furniture.js seatAt)
bool seatAt(World& w, int x, int y, int z, float& top, float& yaw, bool& hasYaw);
void registerBuildEntities();
// Block entities in the original's format, created on first use: chest/barrel {type:'chest', items[27]},
// furnaces {type:'furnace', items[3], burn, burnMax, cook}, shelves/bookcases, piles. Null if the block has none.
json* containerEntity(World& w, int x, int y, int z);
// The other half of a double chest (ui/invui.js chestPartner), if any.
bool chestPartner(World& w, int x, int y, int z, ivec3& out);

} // namespace dq
