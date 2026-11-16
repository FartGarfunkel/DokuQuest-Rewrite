// sim/entity.h — anything that moves: players, creatures, dropped items, falling blocks, arrows,
// boats, carts... Per-type behaviour lives in subclasses or in systems keyed by `type`.
// Sizes follow the original (game/entities.js MOB_TYPES): w = half width, h = height.
#pragma once
#include "common/fs.h"

namespace dq {

class Level;

// the host's clock a little behind, for net replicas to be drawn at (sim/level.cpp; net/client.cpp sets it; < 0: none)
extern double g_netViewTime;

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
  // network (net/): a ghost is a replica of another machine's entity — it is not simulated here; on a client it
  // glides to netPos/netYaw (the server's last snapshot). netLocal entities exist on this device only (never sent).
  bool ghost = false, netLocal = false;
  vec3 netPos{0}; float netYaw = 0;
  // the host's last positions with the host's time of each (net/client.cpp): drawn a little in the past, slid between
  // two known places (easing toward the newest one alone stopped and lurched with every late packet)
  struct NetSample { double t; vec3 p; float yaw; };
  std::vector<NetSample> netHist;
  void netSample(double t, vec3 p, float y) {
    if (!netHist.empty() && t <= netHist.back().t) { if (t < netHist.back().t - 1) netHist.clear(); else return; }   // (a host restarted: its clock went back)
    // stood still a while (nothing sent): it was still there just before this
    if (!netHist.empty() && t - netHist.back().t > 0.15) netHist.push_back({t - 0.1, netHist.back().p, netHist.back().yaw});
    netHist.push_back({t, p, y});
    if (netHist.size() > 24) netHist.erase(netHist.begin(), netHist.begin() + (netHist.size() - 24));
  }
  virtual void glide(double dt) {
    if (g_netViewTime >= 0 && !netHist.empty()) {
      const double rt = g_netViewTime;
      vec3 p; float y;
      if (rt <= netHist.front().t) { p = netHist.front().p; y = netHist.front().yaw; }
      else if (rt >= netHist.back().t) {   // past the newest: on the way it was going for a moment, then wait
        const NetSample& b = netHist.back();
        p = b.p; y = b.yaw;
        if (netHist.size() >= 2) {
          const NetSample& a = netHist[netHist.size() - 2];
          const double span = b.t - a.t, ahead = std::min(rt - b.t, 0.2);
          if (span > 1e-3 && span < 0.5) p = b.p + (b.p - a.p) * (float)(ahead / span);
        }
      } else {
        size_t i = 1; while (i < netHist.size() && netHist[i].t < rt) i++;
        const NetSample& a = netHist[i - 1]; const NetSample& b = netHist[i];
        const float k = (float)((rt - a.t) / std::max(1e-6, b.t - a.t));
        p = glm::mix(a.p, b.p, k); y = a.yaw + std::remainder(b.yaw - a.yaw, 2 * PI) * k;
      }
      pos = p;
      yaw = y;
      return;
    }
    float k = std::min(1.f, (float)dt * 14.f);
    if (glm::distance(netPos, pos) > 8) pos = netPos; else pos += (netPos - pos) * k;
    yaw += std::remainder(netYaw - yaw, 2 * PI) * k;
  }

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
