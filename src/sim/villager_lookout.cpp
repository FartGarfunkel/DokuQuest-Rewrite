// sim/villager_lookout.cpp — lifts for townsfolk, and the lookout. A ride (npcRideLift): the lift called to the floor
// they're on, onto its platform, sent to the floor they want, carried there (sim/moving.cpp carries whoever stands on a
// piece), and off. The lookout: by day, now and then, the first guard of a walled town with a lift in a corner tower
// (gen/structures_villages.cpp townWall) rides it up to the wall walk, steps out through the room up there onto the walk,
// keeps watch out over the land for a minute or two, then rides it down and goes back to the gate. Each step has a time
// limit: a guard who can't get on, or off, gives the turn up (and walks, or climbs the ladder, wherever they go next).
#include "sim/villagers_internal.h"
#include "sim/lift.h"
#include "sim/moving.h"
#include "core/registry.h"
#include <cmath>

namespace dq {

namespace {
bool liftMoving(Level& lv, int realm, ivec3 s) { for (const Body& b : movingBodiesList(lv)) if (b.realm == realm && b.m.machine == s) return true; return false; }
int liftAt(World& w, ivec3 s) { return machineState(w, s).value("at", 0); }
float platY(ivec3 s, int at) { return s.y + 1 + at + 0.5f; }   // (the top of the platform's slab, where whoever's on it stands)
constexpr float RIDE_LIMIT = 60;   // seconds a ride may take, waiting for the lift and all
}

int npcLiftFloorOf(const World& w, ivec3 s, float y) {
  Lift L; if (!liftOfSlider(w, s, L)) return -1;
  int best = -1; float bd = 1.3f;
  for (auto& [stop, at] : L.stops) { const float d = std::fabs(y - platY(s, at) - 0.25f); if (d < bd) { bd = d; best = at; } }
  return best;
}

bool npcLiftIn(const World& w, int x0, int y0, int z0, int x1, int y1, int z1, ivec3& out) {
  for (int y = y0; y <= y1; y++) for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
    if (!w.isLoaded(x, y, z) || reg().block(w.getBlock(x, y, z)).str("machine") != "slider") continue;
    Lift L; if (liftOfSlider(w, ivec3(x, y, z), L) && L.stops.size() >= 2) { out = ivec3(x, y, z); return true; }
  }
  return false;
}

OptMove npcRideLift(Level& lv, Creature& e, NpcMind& M, ivec3 s, int to, float dt, bool* gaveUp) {
  if (gaveUp) *gaveUp = false;
  World& w = lv.world(e.realm);
  auto done = [&]() { M.rideS = ivec3(INT32_MIN); M.rideStage = 0; M.rideT = M.rideStageT = 0; return std::optional<Move>(); };
  auto quit = [&]() { if (gaveUp) *gaveUp = true; return done(); };
  Lift L; if (!liftOfSlider(w, s, L) || L.stops.size() < 2) return quit();
  if (M.rideS != s || M.rideTo != to) { M.rideS = s; M.rideTo = to; M.rideStage = 0; M.rideT = M.rideStageT = 0; }
  M.rideT += dt; M.rideStageT += dt;
  const bool moving = liftMoving(lv, e.realm, s);
  const int at = liftAt(w, s);
  const vec3 mid(s.x + 0.5f, platY(s, at), s.z + 0.5f);
  const float off = std::hypot(e.pos.x - mid.x, e.pos.z - mid.z);
  const bool on = off < 0.42f && std::fabs(e.pos.y - mid.y) < 0.3f;
  const float toShaft = std::atan2(-(mid.x - e.pos.x), -(mid.z - e.pos.z));
  if (M.rideStage == 1) {   // riding: stand still, the lift carries them; there when it's stopped at their floor
    e.vel.x = e.vel.z = 0;
    if (!moving && at == to && M.rideStageT > 0.3f) return done();
    if (!moving && at != to) {
      if (!on) { M.rideStage = 0; M.rideStageT = 0; }   // (off it somehow: start again)
      else if (M.rideStageT > 1.5f) { liftCall(lv, e.realm, s, to); M.rideStageT = 0.5f; }   // (held up, gone back: again)
    }
    if (M.rideT > RIDE_LIMIT * 1.5f) return quit();
    return Move{0, e.yaw};
  }
  const int mine = npcLiftFloorOf(w, s, e.pos.y);
  if (mine == to && !on) return done();   // (on that floor already)
  if (M.rideT > RIDE_LIMIT) return quit();
  if (mine < 0) {   // nowhere near one of its floors: walk to the shaft at the floor nearest them, the lift's own way in
    int best = L.stops.front().second; float bd = 1e9f;
    for (auto& [stop, a] : L.stops) { const float d = std::fabs(e.pos.y - platY(s, a)); if (d < bd) { bd = d; best = a; } }
    if (OptMove m = npcGo(lv, e, M, vec3(s.x + 0.5f, platY(s, best), s.z + 0.5f), dt, e.T->speed * 0.42f)) return m;
    return Move{0, toShaft};
  }
  // the lift to their floor first (and wait clear of the shaft while it comes)
  if (moving) return Move{0, toShaft};
  if (at != mine) { liftCall(lv, e.realm, s, mine); return Move{0, toShaft}; }
  if (on) { liftCall(lv, e.realm, s, to); M.rideStage = 1; M.rideStageT = 0; e.vel.x = e.vel.z = 0; return Move{0, e.yaw}; }
  // onto the platform, to its middle
  if (off < 1.2f) return Move{e.T->speed * 0.25f, toShaft};
  if (OptMove m = npcGo(lv, e, M, mid, dt, e.T->speed * 0.4f)) return m;
  return Move{e.T->speed * 0.25f, toShaft};
}

namespace {
enum { OFF = 0, UP, TO_SPOT, WATCH, DOWN };

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

// where to keep watch from the top of the lift: out on the wall walk just past the tower (the first standing spot
// outside its walls that a way reaches from the platform), else in the room up there beside the shaft
vec3 watchSpot(World& w, const Village& v, ivec3 s, int top, const Creature& e) {
  const vec3 from(s.x + 0.5f, platY(s, top), s.z + 0.5f);
  const int fy = ifloor(from.y + 0.5f);
  for (int r = 3; r <= 6; r++)
    for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
      if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
      // (on the walk, inside the wall: not up on the parapet between the merlons)
      if (s.x + dx <= v.wx0 || s.x + dx >= v.wx1 || s.z + dz <= v.wz0 || s.z + dz >= v.wz1) continue;
      for (int dy : {0, -1, 1}) {
        const float f = npcStandAt(w, s.x + dx, fy + dy, s.z + dz, e.w, e.h);
        if (std::isnan(f)) continue;
        const vec3 to(s.x + dx + 0.5f, f, s.z + dz + 0.5f);
        bool partial = true;
        const auto way = npcPath(w, from, to, e.w, e.h, &partial, 600);
        if (!partial && !way.empty() && way.size() <= (size_t)r + 6) return to;
      }
    }
  for (int f = 0; f < 4; f++) {   // (no way out onto the walk: beside the shaft in the room)
    const float y = npcStandAt(w, s.x + DXL[f], fy, s.z + DZL[f], e.w, e.h);
    if (!std::isnan(y)) return vec3(s.x + DXL[f] + 0.5f, y, s.z + DZL[f] + 0.5f);
  }
  return from;
}
}

OptMove lookoutAI(Level& lv, Creature& e, NpcMind& M, const Village& v, float dt) {
  World& w = lv.world(e.realm);
  const double now = (M.lookClock += dt);   // (its own clock: seconds this guard has been about)
  const bool day = npcPhase(lv) < 0.5;
  M.lookStateT += dt;
  auto give = [&] { M.look = OFF; M.lookStateT = 0; M.nextLookAt = now + 240; M.rideS = ivec3(INT32_MIN); return std::optional<Move>(); };
  if (M.look == OFF) {
    if (!day || now < M.nextLookAt) return std::nullopt;
    Lift L; if (!findLift(w, v, e.pos, M.lookLift, L)) { M.nextLookAt = now + 120; return std::nullopt; }
    M.look = UP; M.lookStateT = 0;
  }
  const ivec3 s = M.lookLift;
  Lift L;
  if (!liftOfSlider(w, s, L) || L.stops.size() < 2) return give();
  const int top = L.stops.back().second, bottom = L.stops.front().second;
  switch (M.look) {
    case UP: {   // to the lift (the ground floor's way into the tower), and up
      bool gave = false;
      if (OptMove m = npcRideLift(lv, e, M, s, top, dt, &gave)) return m;
      if (gave) return give();
      M.lookSpot = watchSpot(w, v, s, top, e);
      M.look = TO_SPOT; M.lookStateT = 0; M.lookLeft = 60 + std::fmod((float)e.id * 7.3f, 60.f);
      return Move{0, e.yaw};
    }
    case TO_SPOT:   // off the platform and out onto the walk
      if (M.lookStateT < 20) if (OptMove m = npcGo(lv, e, M, M.lookSpot, dt, e.T->speed * 0.35f)) return m;
      M.look = WATCH; M.lookStateT = 0;
      [[fallthrough]];
    case WATCH: {   // keep watch, looking out from the town, turning slowly to take it all in
      const float out = std::atan2(-(e.pos.x - v.x), -(e.pos.z - v.z)) + (float)M_PI;
      e.yaw = turnTo(e.yaw, out + std::sin((float)now * 0.2f) * 0.6f, dt * 2);
      if ((M.lookLeft -= dt) <= 0) { M.look = DOWN; M.lookStateT = 0; }
      return Move{0, e.yaw};
    }
    case DOWN: {   // back onto the platform, and down
      bool gave = false;
      if (OptMove m = npcRideLift(lv, e, M, s, bottom, dt, &gave)) return m;
      M.look = OFF; M.lookStateT = 0; M.nextLookAt = now + (gave ? 240 : 180 + std::fmod((float)e.id * 13.1f, 120.f));
      return std::nullopt;
    }
  }
  return give();
}

} // namespace dq
