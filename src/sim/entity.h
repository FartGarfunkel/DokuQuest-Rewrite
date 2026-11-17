// sim/entity.h — anything that moves: players, creatures, dropped items, falling blocks, arrows,
// boats, carts... Per-type behaviour lives in subclasses or in systems keyed by `type`.
// Sizes follow the original (game/entities.js MOB_TYPES): w = half width, h = height.
#pragma once
#include "common/fs.h"

namespace dq {

class Level;

class Entity {
public:
  virtual ~Entity() = default;
  u32 id = 0;
  std::string type;          // "player", "item", "falling_block", or a MOB_TYPES key ("zombie", "cow", ...)
  int realm = 0;
  vec3 pos{0}, vel{0}, prevPos{0};
  float yaw = 0, pitch = 0, bodyYaw = 0, prevYaw = 0;
  float w = 0.3f, h = 1.8f;  // half-width, height
  float hp = 20, maxHp = 20;
  float posture = 0, maxPosture = 40, staggerT = 0;
  int mobLevel = 1;
  bool onGround = false, inWater = false, inLava = false, removed = false, noGravity = false, persistent = false;
  float age = 0, hurtTime = 0, fallDist = 0;
  u32 riding = 0;            // id of the entity this one rides
  json data = json::object();   // per-type state (AI, colour, contents, owner...) saved with the entity
  // network (net/): a ghost is a replica of another machine's entity — it is not simulated here. netLocal entities exist
  // on this device only (never sent).
  bool ghost = false, netLocal = false;
  vec3 netPos{0}; float netYaw = 0;   // the newest place heard (with no timed samples yet: eased toward it)
  // Snapshot interpolation (sim/net_interp.cpp): the sender's positions, each with the time it was true there (put on
  // this computer's clock, nowSeconds), drawn a little in the past — between two known places, never chasing the
  // newest one. How far in the past adapts to how unevenly they arrive; a gap is bridged for a moment on the way it
  // was going, and a correction is eased out rather than jumped.
  struct NetSample { double t; vec3 p; float yaw, pitch, body; };
  std::vector<NetSample> netHist;
  double netDelay = 0.12, netAgeMean = -1, netAgeDev = 0.02, netGap = 0.1, netViewAt = -1, netLastRt = -1;   // (how late they come: their mean and spread)
  vec3 netCorr{0}; float netCorrYaw = 0;
  // the drawn state (netUpdateView): on a joined world it becomes the ghost's pos itself (glide); on the host a guest's
  // body keeps its newest pos for the game's rules and is only DRAWN at the view (client/session.cpp swaps it in)
  bool hasView = false;
  vec3 viewPos{0}, viewVel{0}; float viewYaw = 0, viewPitch = 0, viewBody = 0;
  void netSample(double t, double now, vec3 p, float yaw, float pitch, float body);
  bool netEval(double rt, vec3& p, float& yaw, float& pitch, float& body, vec3* vel = nullptr) const;
  void netUpdateView(double now);
  void netReset() { netHist.clear(); netCorr = vec3(0); netCorrYaw = 0; hasView = false; netViewAt = netLastRt = -1; }
  virtual void glide(double dt);

  virtual void tick(Level& lv, double dt) {}
  virtual json save() const;
  virtual void load(const json& j);
  vec3 eye() const { return pos + vec3(0, h * 0.9f, 0); }
  bool isPlayer() const { return type == "player"; }
};

using EntityFactory = std::function<std::unique_ptr<Entity>(const std::string& type)>;
void registerEntityFactory(const std::string& typeOrPrefix, EntityFactory f);   // "" = fallback for mobs
std::unique_ptr<Entity> createEntity(const std::string& type);

} // namespace dq
