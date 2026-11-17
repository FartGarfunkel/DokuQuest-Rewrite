// sim/villager_lookout.cpp — the lookout: by day, now and then, the first guard of a walled town with a lift in a corner
// tower (gen/structures_villages.cpp townWall) walks onto the lift, calls it up to the wall walk, keeps watch out over the
// land for a while, then rides it down and goes back to the gate. The lift (sim/lift.h) carries them as it carries anyone
// standing on it (sim/moving.cpp). Each step has a time limit: a guard who can't get on, or off, gives the turn up.
#include "sim/villagers_internal.h"
#include "sim/lift.h"
#include "sim/moving.h"
#include "core/registry.h"
#include <cmath>

namespace dq {

namespace {
enum { OFF = 0, TO_LIFT, UP, WATCH, TO_LIFT_TOP, DOWN };
constexpr float STEP_LIMIT = 60;   // seconds any one step may take before the turn is given up

bool liftMoving(Level& lv, int realm, ivec3 s) { for (const Body& b : movingBodiesList(lv)) if (b.realm == realm && b.m.machine == s) return true; return false; }
int liftAt(World& w, ivec3 s) { return machineState(w, s).value("at", 0); }

// the lift in one of the town's corner towers nearest `from` (stone walls only: a palisade's towers are open platforms)
bool findLift(World& w, const Village& v, vec3 from, ivec3& out, Lift& l) {
  if (!v.hasWall || v.wallKind != "stone") return false;
  float best = 1e9f; bool found = false;
  const int cs[4][2] = {{v.wx0, v.wz0}, {v.wx1, v.wz0}, {v.wx0, v.wz1}, {v.wx1, v.wz1}};
  for (auto& c : cs) {
    if (!w.isLoaded(c[0], v.y, c[1])) continue;
    for (int y = v.y - 14; y <= v.y + 24; y++) {
      const ivec3 s(c[0], y, c[1]);
      Lift L;
      if (reg().block(w.getBlock(s.x, s.y, s.z)).str("machine") != "slider" || !liftOfSlider(w, s, L) || L.stops.size() < 2) continue;
      const float d = std::hypot(s.x - from.x, s.z - from.z);
      if (d < best) { best = d; out = s; l = L; found = true; }
      break;
    }
  }
  return found;
}
}

OptMove lookoutAI(Level& lv, Creature& e, NpcMind& M, const Village& v, float dt) {
  World& w = lv.world(e.realm);
  const double now = (M.lookClock += dt);   // (its own clock: seconds this guard has been about)
  const bool day = npcPhase(lv) < 0.5;
  M.lookStateT += dt;
  auto give = [&] { M.look = OFF; M.lookStateT = 0; M.nextLookAt = now + 240; return std::optional<Move>(); };
  if (M.look == OFF) {
    if (!day || now < M.nextLookAt) return std::nullopt;
    Lift L; if (!findLift(w, v, e.pos, M.lookLift, L)) { M.nextLookAt = now + 120; return std::nullopt; }
    M.look = TO_LIFT; M.lookStateT = 0;
  }
  const ivec3 s = M.lookLift;
  Lift L;
  if (!liftOfSlider(w, s, L) || L.stops.size() < 2) return give();
  const int top = L.stops.back().second;
  const float platY = s.y + 1 + liftAt(w, s) + 0.5f;   // (the slab's top, where whoever's on it stands)
  const vec3 onLift(s.x + 0.5f, platY, s.z + 0.5f);
  const bool standingOn = std::hypot(e.pos.x - onLift.x, e.pos.z - onLift.z) < 0.45f && std::fabs(e.pos.y - platY) < 0.7f;
  if (M.lookStateT > STEP_LIMIT && M.look != WATCH) {
    if (e.pos.y > s.y + 3) e.pos = vec3(s.x + 0.5f, s.y + 1.5f, s.z + 0.5f);   // (stuck up top: down the ladder, as it were)
    return give();
  }
  switch (M.look) {
    case TO_LIFT:   // to the platform at the bottom (the lift called down first if it's up)
      if (liftAt(w, s) != 0 && !liftMoving(lv, e.realm, s)) liftCall(lv, e.realm, s, 0);
      if (liftAt(w, s) == 0 && !liftMoving(lv, e.realm, s) && standingOn) { liftCall(lv, e.realm, s, top); M.look = UP; M.lookStateT = 0; return Move{0, e.yaw}; }
      return npcGo(lv, e, M, onLift, dt, 1.f);
    case UP:   // riding: stand still, the lift carries them
      if (!liftMoving(lv, e.realm, s) && liftAt(w, s) == top && M.lookStateT > 0.5f) {
        // a spot on the wall walk beside the shaft, away from the rail
        const ivec3 side = L.rail0 - s, spot(s.x - side.x, s.y + 1 + top, s.z - side.z);
        M.lookSpot = vec3(spot) + vec3(0.5f, 1, 0.5f);
        M.look = WATCH; M.lookStateT = 0; M.lookLeft = 60 + std::fmod((float)e.id * 7.3f, 60.f);
      }
      return Move{0, e.yaw};
    case WATCH: {   // step off and keep watch, looking out from the town
      if (std::hypot(e.pos.x - M.lookSpot.x, e.pos.z - M.lookSpot.z) > 0.6f && M.lookStateT < 10) return npcGo(lv, e, M, M.lookSpot, dt, 0.8f);
      const float out = std::atan2(-(e.pos.x - v.x), -(e.pos.z - v.z)) + (float)M_PI;
      e.yaw = turnTo(e.yaw, out + std::sin((float)now * 0.2f) * 0.6f, dt * 2);
      if ((M.lookLeft -= dt) <= 0) { M.look = TO_LIFT_TOP; M.lookStateT = 0; }
      return Move{0, e.yaw};
    }
    case TO_LIFT_TOP:   // back onto the platform, and down
      if (liftAt(w, s) != top && !liftMoving(lv, e.realm, s)) liftCall(lv, e.realm, s, top);
      if (liftAt(w, s) == top && !liftMoving(lv, e.realm, s) && standingOn) { liftCall(lv, e.realm, s, 0); M.look = DOWN; M.lookStateT = 0; return Move{0, e.yaw}; }
      return npcGo(lv, e, M, onLift, dt, 0.8f);
    case DOWN:
      if (!liftMoving(lv, e.realm, s) && liftAt(w, s) == 0 && M.lookStateT > 0.5f) { M.look = OFF; M.nextLookAt = now + 180 + std::fmod((float)e.id * 13.1f, 120.f); }
      return Move{0, e.yaw};
  }
  return std::nullopt;
}

} // namespace dq
