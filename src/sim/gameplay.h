// sim/gameplay.h — shared gameplay services for players, blocks and items: feedback (sounds,
// particles, toasts through Level::fx; DQ events through the EventBus), giving/dropping items,
// block drops (game.js dropBlockItems + obtain.js), XP, the level clock, and the extension hooks
// other areas use to add block/item behaviour without editing these files.
//
// Client feedback names: LevelFx::sound/material use the original sfx names; particles kinds:
//   "burst" {tile, speed}   "text" {text, color}   "splash" {big}   "dust" {speed}   "smoke"   "bubbles"
// EventBus additions (C++): pickup(player,item,n) screenOpen(player,kind,opts) playerDeath(player,cause)
//   playerRespawn(player) toast(player,text,kind) — plus the DQ ones: blockBreak/blockPlace/mobDeath/chat.
#pragma once
#include "sim/level.h"
#include "sim/player.h"
#include "sim/inventory.h"
#include "sim/blockinfo.h"
#include "world/shapes.h"

namespace dq {

class PlayerController;

// ---- feedback ----
void toast(Level& lv, const Player& p, const std::string& msg, const std::string& kind = "");
void toastAll(Level& lv, const std::string& msg, const std::string& kind = "");
void sfxUi(Level& lv, const Player& p, const std::string& name);                // sfx.play (that player only)
void sfxAt(Level& lv, const std::string& name, vec3 pos, float range = 28);      // sfx.playAt
void sfxMaterial(Level& lv, const std::string& mat, const std::string& kind, vec3 pos);
void burst(Level& lv, vec3 pos, const std::string& tile, int count, float speed);
void floatText(Level& lv, const Player& p, vec3 pos, const std::string& text, const std::string& color);
void openScreen(Level& lv, const Player& p, const std::string& kind, const json& opts = json::object());   // EventBus screenOpen

// ---- the level clock: seconds of play since the level opened (the original's g.time) ----
double gameClock(Level& lv);
// Per-level state owned by the gameplay system (freed with the level): levelSlot<T>(lv, "name").
std::shared_ptr<void>& levelSlotRaw(Level& lv, const std::string& key);
template <class T> T& levelSlot(Level& lv, const std::string& key) {
  std::shared_ptr<void>& s = levelSlotRaw(lv, key);
  if (!s) s = std::make_shared<T>();
  return *static_cast<T*>(s.get());
}

// ---- directions ----
int dirOf(float nx, float nz);              // 0 N, 1 E, 2 S, 3 W for a horizontal vector
int lookDir(const Player& p);               // the way the player faces along the ground
int facingMeta(const Player& p);            // the meta for a block with a front, facing the player

// ---- items ----
int giveItem(Level& lv, Player& p, ItemStack s);       // LocalPlayer.give: pop sound + pickup event; returns leftover
// Spawn a dropped item ("item" entity, data["stack"]). scatter: random spread; vel overrides.
Entity* dropItem(Level& lv, int realm, ItemStack s, vec3 pos, bool scatter, float speed = 0, const vec3* vel = nullptr, float delay = 0);
// What a broken block drops (crops, leaves, grass seeds, slabs, candles...). tool may be null.
std::vector<ItemStack> blockDrops(Level& lv, World& w, int x, int y, int z, BlockId id, u8 meta, const ToolInfo* tool, bool washed);
void dropBlockItems(Level& lv, World& w, int x, int y, int z, BlockId id, bool washed, u8 meta = 0, const ToolInfo* tool = nullptr);
void spillEntity(Level& lv, World& w, const json& ent, int x, int y, int z);   // container contents scatter
void grantXP(Level& lv, Player& p, int n, const vec3* at = nullptr);
// A felled tree toppling away from the chopper (blocks: x,y,z,id); drops land along its fall.
Entity* spawnFallingTree(Level& lv, int realm, const std::vector<std::array<int, 4>>& blocks, ivec3 pivot, float dirYaw, const std::vector<ItemStack>& drops);
std::vector<Player*> playersIn(Level& lv, int realm);   // live players in a realm (the original's pawns())
const std::vector<BlockId>& soilIds();                    // SOIL_IDS
bool isMob(const Entity& e);                              // a creature (MOB_TYPES), not an item/block/vehicle

// ---- extension hooks (redstone, camping, enchanting, creatures... register here) ----
struct Placement { int x = 0, y = 0, z = 0; u8 meta = 0; bool merge = false; std::vector<std::array<int, 5>> also; };   // also: x,y,z,id,meta
struct GameplayHooks {
  // placeSpecial: return -1 no opinion, 0 refuse, 1 use `out`
  std::vector<std::function<int(PlayerController&, const RayHit&, BlockId, Placement& out)>> place;
  // using a placed block (before the built-in uses); true = handled
  std::vector<std::function<bool(PlayerController&, const RayHit&, ItemStack* held, const ItemDef* def)>> useBlock;
  // using an item (before the built-in item uses); true = handled
  std::vector<std::function<bool(PlayerController&, ItemStack& held, const ItemDef& def, bool repeat)>> useItem;
  // change what a broken block drops
  std::vector<std::function<void(Level&, World&, int, int, int, BlockId, u8, const ToolInfo*, bool washed, std::vector<ItemStack>&)>> drops;
  // scale a break time (enchantments, effects)
  std::vector<std::function<float(PlayerController&, BlockId, float)>> breakTime;
  // after a block was broken by a player (multi-block things, ropes...)
  std::vector<std::function<void(PlayerController&, int, int, int, BlockId, u8)>> afterBreak;
  // a block's support/attachment changed (redstone parts that hang off blocks)
  std::vector<std::function<void(Level&, World&, int, int, int, BlockId oldId, BlockId newId)>> blockChanged;
  // using something on a creature or vehicle (feeding, shearing, mounting); true = handled
  std::vector<std::function<bool(PlayerController&, Entity&, ItemStack* held, const ItemDef* def)>> useEntity;
  // the tree a sapling grows into (the world generator's treeParts); true = grown
  std::function<bool(World&, int, int, int, const std::string& type, double r)> growTree;
  // light a portal frame at (x,y,z) ("ember" from flint and steel, "sky" from a water bucket); true = lit
  std::function<bool(World&, int, int, int, const std::string& kind)> ignitePortal;
};
GameplayHooks& hooks();

// Registration (called from registerCommands() in sim/register.cpp): level systems, entity factories.
void registerGameplay();

} // namespace dq
