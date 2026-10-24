// sim/portals.h — portals and the way through them (game/portals.js, portallinks.js, the sim half of
// portalview.js, theend.js's portal rules, the portal half of riftarc.js).
//
//   Ember Depths: an obsidian frame (any shape from 2x3 to 21x21) lit with an Ember Igniter, flint and
//                 steel, or fire placed inside it.
//   Skylands:     a glowstone ("glow lantern") frame filled from a water bucket — from any inside face,
//                 even with water already standing in it.
//   The End:      twelve Eyes of Ender in a stronghold's ring of frames open a 3x3 pool (a flat window).
//   Rifts:        free-standing tears (riftarc.js) registered as virtual portals, with a ragged edge.
//
// The stacked dimensions are bands of ONE World (world/dims.h), so a portal pair joins two places in
// the same World; side realms (pocket, his realm) are other Worlds (Level::world(realm)). A portal is a
// window: its pair transform (rotate about the vertical, centre to centre, floor to floor) carries a
// camera, a walker, their speed and their look to the far side, with no loading screen.
// Links are kept in LevelInfo.data["portalLinks"]: "realm:x,y,z" (anchor = lowest, then least x, then
// least z cell) -> {realm, anchor:[x,y,z], pos:[x,y,z]}; always written both ways.
#pragma once
#include "sim/level.h"
#include "sim/player.h"
#include <unordered_map>
#include <unordered_set>

namespace dq {

constexpr int PORTAL_MAX = 21;          // largest opening, like Minecraft
constexpr int NETHER_SCALE = 1;         // Ember Depths distances: 1:1 like the original 0.9.0.90 (rule "portalScale" overrides)
// the obsidian platform a stronghold portal lets you down onto (theend.js END_PLAT [100, 48, 0] in a
// 128-tall End; band-local y doubled for the 256-tall band). End generators should build it here.
constexpr int END_PLAT_X = 100, END_PLAT_LY = 96, END_PLAT_Z = 0;
inline ivec3 endPlatform() { return ivec3(END_PLAT_X, dimBase(DIM_END) + END_PLAT_LY, END_PLAT_Z); }

enum class PortalKind : u8 { Ember = 0, Sky = 1, End = 2, Rift = 3 };

// One portal as a window: a connected set of portal cells in one plane (or a virtual rift).
struct PortalWindow {
  PortalKind kind = PortalKind::Ember;
  int realm = 0;
  BlockId id = 0;
  int axis = 0;                 // 0: spans x (faces +-z), 1: spans z (faces +-x), 2: flat (faces up), 3: rift
  std::vector<ivec3> cells;
  ivec3 anchor{0}, lo{0}, hi{0};// anchor cell, bounding box (inclusive cells)
  glm::dvec3 c{0};              // centre on the portal plane
  double y0 = 0;                // floor (lowest cell y; the surface for a flat one; tear bottom)
  vec3 n{0, 0, 1};              // plane normal (either side is a way in for block portals)
  vec3 along{1, 0, 0};          // across the window, horizontally (rifts)
  float w = 0, h = 0;           // rift size
  bool walk = true;             // can be walked through
  bool torn = false;            // ragged edge (rifts)
  vec3 rim{1.0f, 0.45f, 0.12f}; // glow colour at the edge of the window
  std::string key;              // "realm:x,y,z" for block portals, "rift:<id>" for virtual ones
};

// Where a window looks out / leads.
struct PortalDest {
  int realm = 0;
  glm::dvec3 c{0};
  double y0 = 0;
  vec3 n{0, 0, 1};
  bool ready = false;           // far side loaded and its shape known: can be seen and walked through
  bool linked = false;
};

// The pair transform (portalview.js pvMap): rotation about the vertical by `yaw`, the window's
// centre onto the far centre, floor onto floor.
struct PortalXform {
  int fromRealm = 0, toRealm = 0;
  glm::dvec3 c0{0}, c1{0};
  double y0a = 0, y0b = 0;
  float yaw = 0;
  bool flat = false;
  glm::dvec3 point(glm::dvec3 p) const;
  vec3 dir(vec3 v) const;
};
// `viewer` matters only for rifts (they have no back: from either side you look into the same place)
PortalXform portalXform(const PortalWindow& w, const PortalDest& d, glm::dvec3 viewer);

// A free-standing portal (a rift's tear) — registered by its owner (riftarc.js port).
struct VirtualPortal {
  int realm = 0;
  glm::dvec3 bottom{0};         // centre of the tear's bottom edge
  float w = 2.6f, h = 4.2f, yaw = 0;   // n = (sin yaw, 0, cos yaw)
  int destRealm = 0;
  glm::dvec3 destBottom{0};
  float destYaw = 0;
  bool walk = true, torn = true;
  vec3 rim{0.75f, 0.3f, 1.0f};
};

// ---- frames and lighting (game/portals.js) ----
bool findPortalOpening(const World& w, ivec3 s, BlockId frame, int axis, bool skyRules, std::vector<ivec3>& cells);
bool tryIgniteAt(World& w, ivec3 p, PortalKind kind);                 // light the frame whose opening holds p
bool tryIgnitePortal(World& w, ivec3 hit, ivec3 normal, PortalKind kind);   // clicked block + face (Skylands: any face)
void collapsePortalNear(World& w, ivec3 p);                            // a frame or portal block went: the portal goes
bool endPortalCheck(World& w, ivec3 p);                                // twelve eyes round (p): open the pool
bool portalAnchor(const World& w, vec3 p, ivec3& out);                 // portallinks.js portalAnchor
vec3 portalStand(const World& w, ivec3 anchor);
int portalTargetDim(int fromDim, PortalKind k);                        // Overworld <-> Depths / Skylands
// find a portal of `kind` near (x, z) in band `dim`, or build one on safe ground; returns where to stand
vec3 arrivePortal(World& w, int dim, double x, double z, PortalKind kind, class PortalSystem* idx = nullptr);
inline ivec3 faceNormal(int face) {   // 0..5 = +x -x +y -y +z -z (BlockDef::faces order)
  static const ivec3 N[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  return face >= 0 && face < 6 ? N[face] : ivec3(0);
}

// ---- the portal system: one per Level ----
// EventBus contract (C++ addition, emitted by whoever handles "use item"):
//   itemUse [playerName, itemKey, x, y, z, face]   x,y,z = the block clicked (global y), face 0..5 =
//   the clicked side (+x -x +y -y +z -z), or -1 when nothing is targeted. Emit it BEFORE the item's own
//   action; a handler returning false means "used up": skip the default (placing water/fire). This
//   system lights portals from it (igniters, water bucket, Eye of Ender) and updates the held stack.
// Events it emits: portalEnter [entityId, fromRealm, toRealm, x, y, z, windowKey]
//                  portalLit   [x, y, z, kind("ember"|"sky"|"end"), realm]
class PortalSystem : public LevelSystem, public std::enable_shared_from_this<PortalSystem> {
public:
  explicit PortalSystem(Level& lv);
  ~PortalSystem() override;
  const char* name() const override { return "portals"; }
  void tick(Level& lv, double dt) override;

  // every portal (window) of a realm in its loaded chunks, plus virtual ones there
  const std::vector<PortalWindow>& windows(int realm);
  const PortalWindow* windowByKey(int realm, const std::string& key);
  PortalDest dest(const PortalWindow& w);
  // stream anchors that keep the far ends of portals near a viewer loaded, by realm (radius by distance)
  void farAnchors(int viewerRealm, vec3 viewerPos, int renderDist, std::map<int, std::vector<StreamAnchor>>& out);

  // crossing: call after an entity moved (the client calls it for its local player every frame;
  // tick() does every other entity). Returns true if it went through a portal.
  bool track(Entity& e, double dt, bool fromClient = false);
  void forget(u32 id) { tracks.erase(id); }

  // virtual portals (rift tears)
  void setVirtual(const std::string& id, const VirtualPortal& v);
  void removeVirtual(const std::string& id);

  // links
  json& links();
  void link(int realmA, ivec3 a, vec3 posA, int realmB, ivec3 b, vec3 posB);
  // the portal-cell index (also used by arrivePortal to find a portal near a target quickly)
  bool nearestPortalCell(int realm, BlockId id, int dim, double x, double z, int r, ivec3& out);
  // damage hook (gameplay replaces it with its own damage function); default subtracts health
  std::function<void(Entity&, float amount, const char* type)> hurt;
  u64 epoch() const { return epoch_; }

  // index upkeep (from the per-World hook)
  void onChunkLoaded(World& w, Chunk& c);
  void onChunkUnloading(World& w, Chunk& c);
  void onBlockChanged(World& w, int x, int y, int z, BlockId o, BlockId n);
  void attach(World& w);

private:
  Level& lv;
  int itemUseHandler = 0;
  struct Cell { ivec3 p; BlockId id; u8 axis; };
  struct RealmIdx {
    std::unordered_map<ChunkKey, std::vector<Cell>, ChunkKeyHash> cells;
    u64 version = 1, builtVersion = 0;
    std::vector<PortalWindow> wins;
  };
  std::map<int, RealmIdx> idx;
  std::unordered_set<int> attached;
  std::map<std::string, VirtualPortal> virtuals;
  u64 virtualVersion = 1;
  struct Track { vec3 pos{0}; int realm = 0; float cool = 0, portalTime = 0; bool init = false; };
  std::unordered_map<u32, Track> tracks;
  std::unordered_set<u32> clientTracked;
  struct DestCache { u64 epoch = 0; PortalDest d; };
  std::unordered_map<std::string, DestCache> dests;
  struct Pending { int realm, dim; double x, z; PortalKind kind; };
  std::map<std::string, Pending> pending;     // windows waiting for their far end
  std::unordered_set<std::string> failed;
  u64 epoch_ = 1;
  int guard = 0;                              // re-entrancy (setBlock inside block-change hooks)
  BlockId bEmber = 0, bSky = 0, bEnd = 0, bObsidian = 0, bGlow = 0, bFrame = 0, bFrameEye = 0;
  u8 isCell[MAX_BLOCKS]{};

  void rebuild(int realm, RealmIdx& r);
  void addCell(int realm, const Cell& c);
  void removeCell(int realm, ivec3 p);
  bool onItemUse(const json& args);
  Player* playerNamed(const std::string& n);
  void prelink(const PortalWindow& w, const Pending& p);
  void swap(Entity& e, const PortalWindow& w, const PortalDest& d, bool mirror);
  void ensureEndPlatform(World& w);
  bool flatDest(const PortalWindow& w, PortalDest& d);
  friend vec3 arrivePortal(World&, int, double, double, PortalKind, PortalSystem*);
};

PortalSystem* portalSystem(Level& lv);   // nullptr if not running
void registerPortals();                  // from initCore(): portals + the stacked worlds (seamless.cpp)
void registerSeamless();

} // namespace dq
