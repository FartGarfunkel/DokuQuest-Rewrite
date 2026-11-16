// sim/player.h — the player (port of game/player.js + game/movement.js): movement (walk, sprint,
// sneak, swim, crawl, ladders, ledge hang/shimmy/climb, coyote time and jump buffering), health
// (no hunger: the original has none — rest heals 1 HP / 4 s, food heals), air, XP levels, armour
// slots, and the save record (the original's toJSON keys). Rules that need the level (damage,
// mining, placing...) live in sim/combat, sim/controller and sim/build.
#pragma once
#include "sim/entity.h"
#include "sim/physics.h"
#include "core/itemstack.h"
#include <optional>

namespace dq {

constexpr float EAT_ANIM = 0.75f;   // one bite's arm motion, seconds (the food in hand is bitten down over it)

constexpr int INV_MAIN = 36;     // 0..8 hotbar, 9..35 backpack rows
constexpr int INV_ARMOR = 4;     // head, chest, legs, feet
constexpr int INV_OFFHAND = 1;
constexpr int INV_ENDER = 27;

constexpr float PLAYER_W = 0.3f, PLAYER_H = 1.8f, EYE = 1.62f, EYE_SNEAK = 1.45f;
constexpr float GRAVITY = 28, JUMP_V = 8.6f;
// The view's height going up a step (stairs, slabs, a block's step-up): it rises at a steady rate matched to the walk,
// not an easing that starts and stops at each step (client/play.cpp; your body goes with it). Only a step: in the air
// (a jump) the view keeps up with you, catching up any step it was still climbing.
inline float viewRise(float want, float prev, float horizSpeed, float velY, bool onGround, float dt) {
  if (want <= prev || want - prev >= 1.05f) return want;
  const float rate = onGround ? 0.9f + 1.1f * horizSpeed : std::max(0.f, velY) + 6.f;
  return std::min(want, prev + rate * dt);
}
constexpr float WALK = 4.3f, SPRINT = 5.7f, SNEAK = 1.4f, SWIM = 4.4f;
constexpr float AIR_MAX = 15;    // seconds of breath (ten bubbles)
constexpr float SWIM_H = 0.6f, SWIM_EYE = 0.42f;
inline int xpForLevel(int level) { return 12 + level * 6; }

// One frame of what the person wants (keyboard/mouse and pads merged). The original's convention:
// mz < 0 is forward, mx > 0 is right. *Pressed = this frame's press edge.
struct PlayerInput {
  float mx = 0, mz = 0, lookX = 0, lookY = 0;
  bool jump = false, sneak = false, sprint = false, attack = false, attackPressed = false, use = false, usePressed = false;
  bool drop = false, dropAll = false, pick = false, swap = false, place = false, crawl = false, crawlHeld = false, dodge = false;
  bool hotRow = false;   // the hotbar swaps with the next row of the bag (the rows turn round like pages)
  bool autoJump = false; // walking into a one-block step jumps it (Options: Auto-Jump, with the touch controls)
  int slot = -1, wheel = 0;
};
struct MoveOpts { bool autoJump = false; float slow = 1; bool noLedge = false, noSprint = false; };
struct MoveInfo { float horizSpeed = 0; bool landed = false; };

struct LedgeHang { int bx = 0, bz = 0, cy = 0; float surf = 0; int dx = 0, dz = 0; float t = 0, x = 0, z = 0; bool water = false; float shimPh = 0, shimT = 0, scrapeT = 0; };
struct LedgeClimb { float t = 0; vec3 from{0}, to{0}; float up = 0; };

class Player : public Entity {
public:
  Player();
  std::string name = "Wanderer";
  std::string skin;                 // skin id (file or creator model)
  std::string mode = "survival";    // survival | creative | adventure | spectator
  // inventory (game/inventory.js PlayerInventory): main 36 (0-8 hotbar), armour 4, backpack slot,
  // off hand, belt hook, charm, cape, the quiver (worn beside the backpack: sim/quiver.h), and the ender chest's 27
  std::vector<ItemStack> inv, armor, offhand, back, hook, charm, cape, quiver, ender;
  std::vector<ItemStack> shield;   // the shield slot: strapped on the forearm of the hand with no weapon in it, that hand still free
  bool atkOff = false;             // the swing going is the off hand's (sim/combat.cpp: the left hand's weapon, mirrored)
  ItemStack cursor;                 // stack held by the mouse in inventory screens
  int selected = 0;                 // hotbar slot
  bool flying = false, sneaking = false, sprinting = false, swimming = false, crawling = false, crawlWant = false;
  bool dead = false;
  float eye = EYE;
  float air = AIR_MAX;
  int xp = 0, level = 0, heartCrystals = 0;
  float stamina = 100, maxStamina = 100, dodgeT = 0, dodgeYaw = 0, blockT = 0, parryFlash = 0, guardFlash = 0, secondChanceCd = 0;
  u8 atkMove = 0, atkCombo = 0;
  float atkT = 0, atkYaw = 0, atkGap = 10;
  std::vector<u32> atkHit;
  int statCon = 0, statStr = 0, statAgi = 0, statInt = 0, statLck = 0, statFoc = 0, statPoints = 0;
  float fuel = 0;
  float invuln = 0, hurtFlash = 0, hurtTilt = 0, burn = 0, slow = 0;
  float foodCd = 0, foodPool = 0, eatAnim = 0, attackCd = 0;
  float swingT = 0, eatT = 0, bowPull = 0;
  // What Are They Up To: what the others see them doing (render/activity_layer.cpp sets your own, the net carries it)
  enum : u8 { ACT_TYPING = 1, ACT_SCREEN = 2, ACT_DOZE = 4 };
  u8 activity = 0;
  // the journal held open at their menus (render/book_model.h): a page turned each time this counts up (Q / LB back,
  // RB / another screen on); bookDir the way the last one went (+1 on, -1 back)
  u8 bookFlip = 0; i8 bookDir = 1;
  // what's on their open journal's pages, as the others see it (a small JPEG, base64; ui/book_ui.cpp), and its version
  std::string bookPic, bookLay; u32 bookPicV = 0; double bookPicAt = 0;   // bookPicAt: when it last changed (steady clock s; the host sends it on then, and now and again)   // bookLay: where the book and its ribbons are in the picture (render/journal_model.h)
  // working a station in the world (ui/cinematic.h): which (0 a table, 1 an anvil, 2 enchanting, 3 brewing, 4 a furnace;
  // -1 none), where the working hand is, a count of things set down / hammer blows: the others see the body do it
  i8 stationKind = -1; vec3 workAt{0}; u8 workTap = 0;
  float typeRate = 0;   // characters a second while typing (the bubble's dots bounce faster)
  std::string eatId;   // the food being eaten while eatAnim runs (bitten down in their hand: assets eatModelKey), sent to the others
  bool scoping = false;   // looking through a spyglass (use held): the view narrows (client/play.cpp), the arm up to the eye
  bool sleeping = false;            // in bed (until morning or getting up)
  float regenTimer = 0, drownT = 0, burnT = 0, hotT = 0, cactusT = 0, acidT = 0;
  double lastHurtT = -100;          // level clock (Level::info.time is wrapped; we use a running clock, see sim/gameplay)
  bool hasFallStart = false; float fallStart = 0;
  bool hasLastSafe = false; vec3 lastSafe{0};
  bool gliding = false; float glideBoost = 0;   // elytra (sim/elytra.h): the glide pose flag, a rocket's boost left
  float maxAir = AIR_MAX;
  vec3 spawnPoint{0, -1, 0};        // bed/respawn anchor (y < 0 = world spawn)
  int spawnRealm = 0;
  json grave;                       // {realm,x,y,z} of the last grave, or null
  json carrying;                    // Carry On: {kind:'block'|'mob', ...} or null
  json pocketReturn;
  json stats = json::object();      // blocksMined, mobsKilled, bossesKilled, deaths (+ rpg/skills keys owned by their systems)
  std::string brokeSomething;
  Contacts contacts;
  // movement state
  std::optional<LedgeHang> hang;
  std::optional<LedgeClimb> climb;
  bool hitWall = false, hitWallLast = false, hitCeiling = false, bounced = false, justGrabbed = false;
  float airStep = 0, airT = 0, jumpBuf = 0, crawlHold = 0;
  bool jumpHeld = false, mvJumpHeld = false, jumped = false, shimSound = false;
  // animation (for renderers: view bob, arm swing, equip)
  float bob = 0, bobAmt = 0, swing = 0, equip = 1, stepAcc = 0, swimPh = 0, dustAcc = 0;
  bool swinging = false, wasWater = false;

  ItemStack& held() { return inv[selected]; }
  bool creative() const { return mode == "creative"; }
  float health() const { return hp; }
  int give(ItemStack s);            // main slots then an equipped backpack; returns leftover count
  float heal(float n);              // returns the amount healed
  void recalcMax();                 // 20 + 2 per heart crystal (max 10) + charm hearts
  int addXP(int n);                 // returns levels gained

  // movement (Player.move + movement.js): updates pos/vel/onGround/eye/h
  MoveInfo move(const World& w, const PlayerInput& in, float dt, const MoveOpts& opts);

  json save() const override;
  void load(const json& j) override;
private:
  MoveInfo baseMove(const World& w, const PlayerInput& in, float dt, const MoveOpts& opts);
  MoveInfo climbStep(const World& w, float dt);
};

// ledges by the blocks' real heights (player.js 0.9.0.41)
bool ledgeSurface(const World& w, int x, int cy, int z, float& surf);
bool ledgeRoom(const World& w, int bx, int bz, float s, float halfW);

} // namespace dq
