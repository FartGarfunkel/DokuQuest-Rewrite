// sim/content.h — life in the world: the shared plumbing of the content features (maps, farming, fishing,
// camping, travel, elytra, the stable, far places). One LevelSystem (ContentSystem) listens to the game's
// events (itemUse, blockBreak, netAction) and ticks every feature; each feature lives in its own file
// (sim/maps.cpp, farming.cpp, fishing.cpp, camping.cpp, travel.cpp, elytra.cpp, stable.cpp, places.cpp).
//
// Other areas plug in through ContentHooks (all optional; the defaults are plain and safe):
//   hurtPlayer / grantXp / dropItem   gameplay's combat, experience and dropped-item spawning
//   capeSlot                          the characters area's cape slot (elytra are worn there)
//   placesNear / villageNear / villageById / dungeonById   structures and villages (discovery, waystones, map marks)
#pragma once
#include "sim/level.h"
#include "sim/player.h"

namespace dq {

struct PlaceInfo {          // a named place in the world (structures.js S: id, name, box, y)
  std::string id, name;
  int realm = REALM_MAIN;
  int x0 = 0, z0 = 0, x1 = 0, z1 = 0, y = -1;   // y global (-1 = any height)
};
struct VillageInfo {        // villages.js v: id, name, centre, radius, buildings
  std::string id, name;
  int x = 0, y = 0, z = 0, r = 24;              // y global (the square's ground)
  std::vector<ivec4> blds;                      // x0, z0, x1, z1 of each building
};

struct ContentHooks {
  std::function<void(Level&, Player&, float amount, const std::string& type)> hurtPlayer;   // default: hp -= amount
  std::function<void(Level&, Player&, int xp, vec3 at)> grantXp;                             // default: p.xp += xp
  std::function<Entity*(Level&, int realm, const ItemStack&, vec3 pos, vec3 vel)> dropItem;  // default: an "item" entity
  std::function<const ItemStack*(const Player&)> capeSlot;                                   // default: none (elytra in the chest slot count)
  std::function<std::vector<PlaceInfo>(Level&, int realm, vec3 pos, float pad)> placesNear;
  std::function<std::optional<VillageInfo>(Level&, vec3 pos, float radius)> villageNear;
  std::function<std::optional<VillageInfo>(Level&, const std::string& id)> villageById;
  std::function<std::optional<PlaceInfo>(Level&, const std::string& id)> dungeonById;
};
ContentHooks& contentHooks();

// ---- helpers every feature uses (the original's names where there was one) ----
Player* findPlayer(Level& lv, const std::string& name);
std::vector<Player*> levelPlayers(Level& lv, bool withGhosts = false);
float frand();                                     // Math.random()
inline float frand2() { return frand() - 0.5f; }   // Math.random() - 0.5
vec3 lookDir(const Player& p);                     // player.look()
constexpr float EYE = 1.62f, PLAYER_H = 1.8f, SWIM_H = 0.6f, REACH = 5;
inline bool creativeOf(const Player& p) { return p.mode == "creative"; }
void toast(Level& lv, const Player& p, const std::string& text, const std::string& kind = "");
void sfxUi(Level& lv, const Player& p, const std::string& name);                  // sfx.play (that player)
void sfxAt(Level& lv, const std::string& name, vec3 at, float range = 28);        // sfx.playAt
void hurtPlayer(Level& lv, Player& p, float amount, const std::string& type);
void grantXp(Level& lv, Player& p, int xp, vec3 at);
Entity* dropItem(Level& lv, int realm, const ItemStack& s, vec3 pos, vec3 vel = vec3(0));
void giveOrDrop(Level& lv, Player& p, const ItemStack& s);                          // lp.give()
void useUpHeld(Player& p, int n = 1);                                               // held.n -= n (not in creative)
bool wearHeld(Level& lv, Player& p, int n = 1);                                     // tool wear; true = it broke
int countItem(const Player& p, const std::string& id);
int findSafeY(World& w, int x, int z, int dim, int top = -1);                      // global y to stand at (-1 none)
inline bool isAirOrSoft(BlockId id) { return id == AIR || (reg().tReplaceable[id] && !reg().tLiquid[id]); }
std::string jsDimName(int jsDim);                                                    // DIM_NAME
int jsDimOf(int realm, int y);                                                       // the original's dimension number here
int jsDimOf(const Entity& e);
std::string capFirst(std::string s);
std::string newId(const char* prefix = "");                                          // Date.now().toString(36)... ids
double playTime(Level& lv);                                                          // g.meta.playTime (seconds)

// the event a screen should open for a player on this device: contentScreen [playerName, screenId, args]
// screen ids: "map" {stack slot}, "waystones" {here}, "stable" {}, "places" {}
void openContentScreen(Level& lv, const Player& p, const std::string& id, const json& args = json::object());

// A host-side action asked for by a client (lv.net->action → event netAction [player, "content", {k, ...}]).
// On the host (or single player) it runs at once.
void contentAction(Level& lv, Player& p, const std::string& kind, const json& args);

// ---- the per-feature entry points ContentSystem calls (each implemented in its feature file) ----
struct UseCtx {
  Level& lv; Player& p; std::string item; ivec3 hit{0}; int face = -1;   // face -1 = nothing targeted
  bool hasHit() const { return face >= 0; }
  ivec3 normal() const { static const ivec3 N[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}; return face >= 0 && face < 6 ? N[face] : ivec3(0); }
  ivec3 placeAt() const { return hit + normal(); }
  World& world() const { return lv.world(p.realm); }
  ItemStack& held() const { return p.held(); }
};
// each returns true if it used the click (the default action is then skipped)
bool mapsUse(UseCtx& u);
bool farmingUse(UseCtx& u);
bool fishingUse(UseCtx& u);
bool campingUse(UseCtx& u);
bool travelUse(UseCtx& u);
bool elytraUse(UseCtx& u);
bool stableUse(UseCtx& u);
// blockBreak (before the block goes): false = this feature did the break itself
bool campingBreak(Level& lv, Player& p, int x, int y, int z, BlockId id, u8 meta);
// ticks (fixed step)
void mapsTick(Level& lv, double dt);
void fishingTick(Level& lv, double dt);
void campingTick(Level& lv, double dt);
void travelTick(Level& lv, double dt);
void elytraTick(Level& lv, double dt);
void stableTick(Level& lv, double dt);
void placesTick(Level& lv, double dt);
void mapsSave(Level& lv);
// host-side actions from clients
bool stableAction(Level& lv, Player& p, const json& a);

void registerContent();   // called once from initCore(): entity factories + the ContentSystem

} // namespace dq
