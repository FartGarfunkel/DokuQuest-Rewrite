// sim/seamless.h — the stacked worlds without a loading screen (game/seamless.js, worlds.js,
// worldupdate.js stackedTick/slow fall, theend.js void crossings).
//
// In C++ the bands touch, so crossing is just moving: this system adds what the original did around
// the edges — the cloud bank under the Skylands that lets a fall down gently, the winds that carry you
// from the Overworld's top onto an island, breaking through the crust into the Ember Depths, the void
// crust between the Depths and the End (and the void winds there), the End's void below y -32 that
// kills, the notices, first-visit rewards and "dimensionChange" events. Preparing the neighbouring band
// in the background is World::stream's nearDims (world/world.cpp).
// Events: dimensionChange [entityId, playerName, fromDim, toDim, fromRealm, toRealm]
#pragma once
#include "sim/portals.h"

namespace dq {

class SeamlessSystem : public LevelSystem {
public:
  explicit SeamlessSystem(Level& l) : lv(l) {}
  const char* name() const override { return "stackedWorlds"; }
  void tick(Level& l, double dt) override;
  // a portal (or anything else) moved this entity: no band-crossing notices; End portal winds
  void arrived(Entity& e, PortalKind kind, int fromDim, int toDim);
  // how deep in a cloud bank between the worlds a height is (0..0.93) — the sky renderer may use it
  static float cloudAt(double y);
  vec3 skyLanding(double x, double z) const;   // the nearest island surface (global y)
  vec3 endLanding(double x, double z) const;

  struct State {
    bool init = false;
    vec3 lastPos{0}; int lastRealm = 0; float lastVy = 0;
    float slowFallT = 0; bool cloudFall = false;
    int updraft = -1; vec3 upCol{0}, upTo{0};   // stage 0 along under the islands, 1 up, 2 across
    bool drift = false; vec3 driftTo{0};         // the End's void winds
    float acc[3] = {0, 0, 0};                    // particle accumulators
  };
  State* state(u32 id) { auto it = st.find(id); return it == st.end() ? nullptr : &it->second; }

private:
  Level& lv;
  std::unordered_map<u32, State> st;
  void crossed(class Player& p, State& s, int from, int to);
  void firstVisit(class Player& p, const char* flag, int xp);
  void startUpdraft(class Player& p, State& s);
  void puff(State& s, int slot, float rate, double dt, const char* kind, vec3 pos, const json& extra = nullptr);
};

} // namespace dq
