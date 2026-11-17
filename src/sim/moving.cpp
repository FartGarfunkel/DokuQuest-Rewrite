// sim/moving.cpp — see moving.h.
#include "sim/moving.h"
#include "sim/glue.h"
#include "sim/blockinfo.h"
#include "sim/rotate.h"
#include "sim/gameplay.h"
#include "sim/worldsys.h"
#include "sim/machines.h"
#include "sim/physics.h"
#include "sim/lift.h"
#include "sim/events.h"
#include "sim/entity.h"
#include "common/common.h"
#include <set>
#include "core/registry.h"
#include <cmath>

namespace dq {

float Body::fraction() const { const float f = std::clamp(t / std::max(dur, 1e-3f), 0.f, 1.f); return f * f * (3 - 2 * f); }

mat4 Body::transform() const {
  const float f = fraction();
  if (m.kind == 1) return glm::translate(mat4(1), vec3(faceDir(m.face)) * (float)m.distance * f);
  const float ang = m.quarters * (float)M_PI * 0.5f * f;
  const vec3 c(0.5f);
  const int ax = motionAxis(m);
  const vec3 axis = ax == 0 ? vec3(0, 1, 0) : ax == 1 ? vec3(1, 0, 0) : vec3(0, 0, 1);
  const float a = ax == 1 ? ang : -ang;   // (the same turn as moving_piece.cpp turned())
  return glm::translate(mat4(1), c) * glm::rotate(mat4(1), a, axis) * glm::translate(mat4(1), -c);
}

bool isMachine(BlockId id) { const std::string m = reg().block(id).str("machine"); return m == "hinge" || m == "slider"; }

int machineFace(const World& w, ivec3 p) {
  static const int D6F[6] = {3, 2, 5, 4, 1, 0};   // six-way facing (0 down, 1 up, 2 north, 3 south, 4 west, 5 east) -> face index
  const int f = w.getMeta(p.x, p.y, p.z) & 7;   // (the facing's own bits: % 6 turned a meta with other bits set into another face)
  return f < 6 ? D6F[f] : D6F[1];
}

json& machineState(World& w, ivec3 p) {
  json* e = w.blockEntity(p.x, p.y, p.z, true);
  if (!e) { static thread_local json none; none = json::object(); return none; }   // (its land isn't loaded: nothing kept)
  if (!e->is_object()) *e = json::object();
  if (!e->contains("type")) (*e)["type"] = "machine_state";
  return *e;
}

bool machineOpen(const World& w, ivec3 p) {
  const json* e = const_cast<World&>(w).blockEntity(p.x, p.y, p.z);
  return e && e->is_object() && e->value("open", false);
}

namespace {
// a face index turned by the motion's whole quarter turns
int turnFace(const Motion& m, int f) {
  if (m.kind == 1) return f;
  Motion u = m; u.machine = ivec3(0);
  const ivec3 d = movedCell(u, faceDir(f));
  for (int g = 0; g < 6; g++) if (faceDir(g) == d) return g;
  return f;
}
u8 turnMeta(const Motion& m, BlockId id, u8 meta) {
  if (m.kind == 1) return meta;
  const int ax = motionAxis(m);
  return ax == 0 ? rotateMetaY(id, meta, m.quarters) : rotateMetaAbout(id, meta, ax, m.quarters);
}

u8 turnCarve(const Motion& m, u8 c) {
  if (m.kind == 1 || !c) return c;
  const int ax = motionAxis(m);
  return ax == 0 ? rotateCarveY(c, m.quarters) : rotateCarveAbout(c, ax, m.quarters);
}

class MovingBodies : public LevelSystem {
public:
  explicit MovingBodies(Level& l) : lv(l) {
    all().push_back(this);
    // §4 a joined game: the host sends each body as it sets off and when it's set down; here it is only drawn and stood on
    if (lv.events) {
      hStart = lv.events->on("movingStart", [this](const json& a) { if (lv.isRemote()) fromNet(a); return true; });
      hEnd = lv.events->on("movingEnd", [this](const json& a) {
        if (!lv.isRemote() || !a.is_object()) return true;
        const u32 id = a.value("id", 0u);
        bodies.erase(std::remove_if(bodies.begin(), bodies.end(), [&](const Body& b) { return b.id == id; }), bodies.end());
        return true;
      });
    }
  }
  ~MovingBodies() override {
    auto& a = all(); a.erase(std::remove(a.begin(), a.end(), this), a.end());
    if (lv.events) { lv.events->off(hStart); lv.events->off(hEnd); }
  }
  int hStart = -1, hEnd = -1;
  static json toNet(const Body& b) {
    json bl = json::array();
    for (const BodyBlock& k : b.blocks) bl.push_back(json::array({k.rel.x, k.rel.y, k.rel.z, reg().block(k.id).key, k.meta, k.carve}));
    return json{{"id", b.id}, {"realm", b.realm}, {"kind", b.m.kind}, {"mx", b.m.machine.x}, {"my", b.m.machine.y}, {"mz", b.m.machine.z}, {"face", b.m.face},
                {"q", b.m.quarters}, {"d", b.m.distance}, {"reach", b.m.reach}, {"t", b.t}, {"dur", b.dur}, {"blocks", bl}};
  }
  void fromNet(const json& a) {
    if (!a.is_object() || !a.contains("blocks")) return;
    Body b; b.id = a.value("id", 0u); b.realm = a.value("realm", 0);
    b.m.kind = a.value("kind", 0); b.m.machine = ivec3(a.value("mx", 0), a.value("my", 0), a.value("mz", 0)); b.m.face = a.value("face", 0);
    b.m.quarters = a.value("q", 0); b.m.distance = a.value("d", 0); b.m.reach = a.value("reach", 1);
    b.t = a.value("t", 0.f); b.dur = std::max(0.05f, a.value("dur", 1.f));
    for (auto& k : a["blocks"]) if (k.is_array() && k.size() >= 6) b.blocks.push_back({ivec3(k[0].get<int>(), k[1].get<int>(), k[2].get<int>()), reg().blockId(k[3].get<std::string>()), (u8)k[4].get<int>(), json(), (u8)k[5].get<int>()});
    bodies.erase(std::remove_if(bodies.begin(), bodies.end(), [&](const Body& o) { return o.id == b.id; }), bodies.end());
    b.boxes = boxesAt(b, worldXf(b, b.t));
    bodies.push_back(std::move(b));
  }
  void announce(const char* ev, const json& a) { if (lv.events && !lv.isRemote()) lv.events->emit("netBroadcast", json::array({ev, a})); }
  static std::vector<MovingBodies*>& all() { static std::vector<MovingBodies*> v; return v; }
  World* worldOf(int realm) { return lv.worldIfOpen(realm); }
  const char* name() const override { return "moving"; }
  std::vector<Body> bodies;
  u32 nextId = 1;

  bool start(int realm, ivec3 machine, std::string* why, float speed = 1.f, bool spin = false) {
    World& w = lv.world(realm);
    if (!isMachine(w.getBlock(machine.x, machine.y, machine.z))) { if (why) *why = "not a hinge or a slider"; return false; }
    for (const Body& b : bodies) if (b.realm == realm && b.m.machine == machine) { if (why) *why = "already moving"; return false; }
    json& st = machineState(w, machine);
    const bool open = spin ? false : st.value("open", false);
    Motion m; m.machine = machine; m.face = machineFace(w, machine);
    m.kind = reg().block(w.getBlock(machine.x, machine.y, machine.z)).str("machine") == "slider" ? 1 : 0;
    int q = spin ? 1 : st.value("quarters", 1); if (q == 0) q = 1;   // (turning for good: an axle turns it a quarter at a time; by hand a quarter)
    int d = std::clamp(st.value("distance", 1), 1, 16);
    if (open) { q = st.value("usedQ", q); d = std::clamp(st.value("usedD", d), 1, 16); }   // (shut the way it opened, whatever it's set to now)
    Lift lift; const bool isLift = m.kind == 1 && !spin && liftOfSlider(w, machine, lift);
    int liftAt = 0, liftTo = 0;
    if (isLift) {   // along the rail to the floor asked for ("goto": a stop's button), or else the next one on
      liftAt = std::clamp(st.value("at", 0), 0, lift.maxAt());
      liftTo = st.contains("goto") && st["goto"].is_number() ? std::clamp(st["goto"].get<int>(), 0, lift.maxAt()) : liftNext(lift, liftAt);
      st.erase("goto");
      if (liftTo == liftAt) { if (why) *why = ""; return false; }   // (already there)
      m.distance = liftTo - liftAt; m.reach = 1 + liftAt;
    }
    else if (m.kind == 0) m.quarters = open ? -q : q;
    else { m.distance = open ? -d : d; m.reach = open ? 1 + d : 1; }
    Piece p;
    if (!gatherPiece(w, m, p, why)) return false;
    // a machine riding in it that stands open or is moving: its own piece would be left behind (shut it first)
    for (const ivec3& c : p.cells) {
      if (!isMachine(w.getBlock(c.x, c.y, c.z))) continue;
      bool busy = machineOpen(w, c);
      for (const Body& o : bodies) if (o.realm == realm && o.m.machine == c) busy = true;
      if (busy) { if (why) *why = "a hinge or slider in it is open or moving: shut it first"; return false; }
    }
    std::vector<ivec3> breakable; ivec3 at(0);
    if (!pathClear(w, m, p, &breakable, &at)) {
      if (why) *why = "something is in the way";
      lv.fx->particles("burst", vec3(at) + vec3(0.5f), 6, json{{"item", "stone"}, {"speed", 0.2}});
      sfxAt(lv, "door_locked", vec3(machine) + vec3(0.5f));
      return false;
    }
    for (const ivec3& b : breakable) w.setBlock(b.x, b.y, b.z, AIR);
    Body body; body.id = nextId++; body.realm = realm; body.m = m; body.spin = spin; body.lift = isLift;
    body.dur = (m.kind == 1 ? std::abs(m.distance) / 2.f : std::abs(m.quarters) * 1.f) / std::clamp(speed, 0.25f, 2.f);
    for (const ivec3& c : p.cells) {
      const json* e = w.blockEntity(c.x, c.y, c.z);
      body.blocks.push_back({c - machine, w.getBlock(c.x, c.y, c.z), w.getMeta(c.x, c.y, c.z), e ? *e : json(), w.getCarve(c.x, c.y, c.z)});
    }
    { QuietEdits quiet;   // (lifted whole: a door or torch on a lifted plank must not drop as it goes, nor sand fall)
      for (const ivec3& c : p.cells) { w.setBlockEntity(c.x, c.y, c.z, nullptr); w.setBlock(c.x, c.y, c.z, AIR); } }
    if (isLift) { st["from"] = liftAt; st["at"] = liftTo; st["open"] = liftTo > 0; liftMarkStops(w, lift, -1); }
    else {
      if (!spin) st["open"] = !open;
      if (!open && !spin) { st["usedQ"] = q; st["usedD"] = d; }
    }   // (cleared when it's really shut again: finish())
    w.blockEntityChanged(machine.x, machine.y, machine.z);
    sfxAt(lv, m.kind == 1 ? "piston_out" : "door_open", vec3(machine) + vec3(0.5f));
    body.boxes = boxesAt(body, worldXf(body, body.t));
    announce("movingStart", toNet(body));
    bodies.push_back(std::move(body));
    return true;
  }

  void finish(Body& b, bool forceBack = false) {
    if (lv.isRemote()) return;   // (a guest: the host sets it down and its blocks come over as usual)
    World* wp = lv.worldIfOpen(b.realm);
    if (!wp) return;
    World& w = *wp;
    QuietEdits quiet;   // (set down whole, the same way)
    auto freeCell = [&](ivec3 p) { const BlockId id = w.getBlock(p.x, p.y, p.z); return w.isLoaded(p.x, p.y, p.z) && (!id || reg().tReplaceable[id] || reg().tLiquid[id]); };
    // all of it where it was going, or (something put in the way since it set off) all of it back: never split
    bool arrive = true;
    if (forceBack) arrive = false;
    else for (const BodyBlock& k : b.blocks) if (!freeCell(movedCell(b.m, b.m.machine + k.rel))) { arrive = false; break; }
    if (!arrive && b.lift) {   // (back where it set off from)
      json& st = machineState(w, b.m.machine);
      st["at"] = st.value("from", 0); st["open"] = st.value("from", 0) > 0;
      w.blockEntityChanged(b.m.machine.x, b.m.machine.y, b.m.machine.z);
    }
    else if (!arrive && !b.spin) {
      json& st = machineState(w, b.m.machine);
      st["open"] = !st.value("open", false);   // (it never got there: as it was)
      w.blockEntityChanged(b.m.machine.x, b.m.machine.y, b.m.machine.z);
    }
    { json& st = machineState(w, b.m.machine); if (!st.value("open", false)) { st.erase("usedQ"); st.erase("usedD"); } }   // (shut: how it opened is done with)
    for (BodyBlock& k : b.blocks) {
      const ivec3 from = b.m.machine + k.rel, to = arrive ? movedCell(b.m, from) : from;
      u8 meta = arrive ? turnMeta(b.m, k.id, k.meta) : k.meta, carve = arrive ? turnCarve(b.m, k.carve) : k.carve;
      json e = k.entity;
      if (arrive && e.is_object() && e.contains("glue")) {   // the glue's faces turn with it
        int g = e["glue"], ng = 0;
        for (int f = 0; f < 6; f++) if (g >> f & 1) ng |= 1 << turnFace(b.m, f);
        e["glue"] = ng;
      }
      auto freeAt = [&](ivec3 p) { const BlockId id = w.getBlock(p.x, p.y, p.z); return w.isLoaded(p.x, p.y, p.z) && (!id || reg().tReplaceable[id] || reg().tLiquid[id]); };
      ivec3 place = to;
      if (!freeAt(place)) { place = from; meta = k.meta; e = k.entity; carve = k.carve; }   // (filled since it set off: back where it was)
      if (!freeAt(place)) {   // nowhere: never lost, it drops (a container's contents too)
        const std::string drop = reg().block(k.id).drop.empty() ? reg().block(k.id).key : reg().block(k.id).drop;
        const bool secondHalf = (bhas(k.id, BF::Door) && (k.meta & 16)) || (bhas(k.id, BF::Bed) && (k.meta & 4));   // (a door or bed is one item: its other half drops nothing)
        if (!secondHalf) dropItem(lv, b.realm, ItemStack(drop, 1), vec3(to) + vec3(0.5f), true);
        if (e.is_object() && e.contains("items") && e["items"].is_array()) for (auto& it : e["items"]) if (!it.is_null()) dropItem(lv, b.realm, ItemStack::fromJson(it), vec3(to) + vec3(0.5f), true);
        continue;
      }
      w.setBlock(place.x, place.y, place.z, k.id, meta);
      if (carve) w.setCarve(place.x, place.y, place.z, carve);
      if (!e.is_null()) w.setBlockEntity(place.x, place.y, place.z, e);
    }
    sfxAt(lv, b.m.kind == 1 ? "piston_in" : "door_close", vec3(b.m.machine) + vec3(0.5f));
    announce("movingEnd", json{{"id", b.id}});
    if (arrive && lv.events) {   // where its blocks went (strung ropes' ends follow: sim/strung.cpp)
      json mv = json::array();
      for (const BodyBlock& k : b.blocks) { const ivec3 f = b.m.machine + k.rel, t = movedCell(b.m, f); mv.push_back(json::array({f.x, f.y, f.z, t.x, t.y, t.z})); }
      lv.events->emit("movingMoved", json{{"realm", b.realm}, {"moves", mv}});
    }
    if (b.lift) { Lift l; if (liftOfSlider(w, b.m.machine, l)) liftMarkStops(w, l, machineState(w, b.m.machine).value("at", 0)); }
  }

  void finishAllNow() { auto bs = std::move(bodies); bodies.clear(); for (Body& b : bs) finish(b); }

  // machines something drives (a signal, an axle): realm and place
  std::set<std::pair<int, std::array<int, 3>>> driven, axled;
  double axleT = 0;
  bool moving(int realm, ivec3 p) const { for (const Body& b : bodies) if (b.realm == realm && b.m.machine == p) return true; return false; }

  // the piece's placement in the world at time t (its machine's corner at the origin of Body::transform)
  static mat4 worldXf(const Body& b, float t) { Body c; c.m = b.m; c.t = t; c.dur = b.dur; return glm::translate(mat4(1), vec3(b.m.machine)) * c.transform(); }
  static std::vector<std::array<float, 6>> boxesAt(const Body& b, const mat4& X) {
    std::vector<std::array<float, 6>> out;
    for (const BodyBlock& k : b.blocks) {
      if (!reg().tSolid[k.id]) continue;
      const vec3 c = vec3(X * vec4(vec3(k.rel) + vec3(0.5f), 1));
      out.push_back({c.x - 0.5f, c.y - 0.5f, c.z - 0.5f, c.x + 0.5f, c.y + 0.5f, c.z + 0.5f});
    }
    return out;
  }
  static void extraBoxes(const World& w, double x0, double y0, double z0, double x1, double y1, double z1, std::vector<DBox>& out) {
    for (MovingBodies* m : all()) for (const Body& b : m->bodies) {
      if (m->worldOf(b.realm) != &w) continue;
      for (auto& q : b.boxes) if (q[3] > x0 && q[0] < x1 && q[4] > y0 && q[1] < y1 && q[5] > z0 && q[2] < z1) out.push_back({q[0], q[1], q[2], q[3], q[4], q[5]});
    }
  }

  // §4: anything standing on the piece rides it; anything in its way is pushed along. Something with nowhere to go holds it
  // up; held for 0.6 s, the piece goes back. Returns false when it's held up this tick.
  bool carry(Body& b, float t1) {
    World* wp = lv.worldIfOpen(b.realm); if (!wp) return true;
    const mat4 X0 = worldXf(b, b.t), X1 = worldXf(b, t1), D = X1 * glm::inverse(X0);
    const auto& B0 = b.boxes; const auto B1 = boxesAt(b, X1);
    std::vector<std::pair<Entity*, vec3>> moves;
    for (auto& up : lv.entities()) {
      Entity* e = up.get();
      if (e->removed || e->realm != b.realm || e->type == "moving" || (lv.isRemote() && e->ghost)) continue;
      const float hw = e->w, h = e->h;
      const vec3 p = e->pos;
      bool rider = false, pushed = false;
      for (auto& q : B0) if (p.x + hw > q[0] && p.x - hw < q[3] && p.z + hw > q[2] && p.z - hw < q[5] && std::fabs(p.y - q[4]) < 0.15f) { rider = true; break; }
      if (!rider) for (auto& q : B1) if (p.x + hw - 0.01f > q[0] && p.x - hw + 0.01f < q[3] && p.z + hw - 0.01f > q[2] && p.z - hw + 0.01f < q[5] && p.y + h - 0.01f > q[1] && p.y + 0.01f < q[4]) { pushed = true; break; }
      if (!rider && !pushed) continue;
      vec3 np = vec3(D * vec4(p, 1));
      if (pushed && b.m.kind == 1) {   // a slide: shoved clear of the face that hit it
        const vec3 d = vec3(faceDir(b.m.face)) * (b.m.distance < 0 ? -1.f : 1.f);
        for (auto& q : B1) {
          if (!(np.x + hw > q[0] && np.x - hw < q[3] && np.z + hw > q[2] && np.z - hw < q[5] && np.y + h > q[1] && np.y < q[4])) continue;
          if (d.x > 0) np.x = std::max(np.x, q[3] + hw + 0.001f); if (d.x < 0) np.x = std::min(np.x, q[0] - hw - 0.001f);
          if (d.z > 0) np.z = std::max(np.z, q[5] + hw + 0.001f); if (d.z < 0) np.z = std::min(np.z, q[2] - hw - 0.001f);
          if (d.y > 0) np.y = std::max(np.y, q[4] + 0.001f); if (d.y < 0) np.y = std::min(np.y, q[1] - h - 0.001f);
        }
      }
      t_noExtraBoxes = true;
      const bool room = !collides(*wp, np.x - hw + 0.02, np.y + 0.02, np.z - hw + 0.02, np.x + hw - 0.02, np.y + h - 0.02, np.z + hw - 0.02);
      t_noExtraBoxes = false;
      if (!room) { if (lv.isRemote()) continue; return false; }   // (a guest never holds it up: the host decides)
      moves.push_back({e, np});
    }
    for (auto& [e, np] : moves) { e->pos = np; if (e->vel.y < 0 && np.y >= e->pos.y) e->vel.y = 0; }
    return true;
  }

  double clock = 0;
  void tick(Level&, double dt) override {
    attachHooks();
    clock += dt;
    for (size_t i = 0; i < bodies.size();) {
      Body& b = bodies[i];
      if (lv.isRemote() && b.t >= b.dur) {   // (a guest: there until the host says it's down; gone after 3 s whatever)
        if ((b.stuck += (float)dt) > 3.f) bodies.erase(bodies.begin() + i); else i++;
        continue;
      }
      const float t1 = std::min(b.t + (float)dt, b.dur);
      if (!carry(b, t1)) {
        if ((b.stuck += (float)dt) > 0.6f) { Body c = std::move(b); bodies.erase(bodies.begin() + i); finish(c, true); continue; }
        i++; continue;
      }
      b.stuck = 0; b.t = t1;
      b.boxes = boxesAt(b, worldXf(b, b.t));
      if (lv.isRemote()) { i++; continue; }
      if (b.t >= b.dur) { Body c = std::move(b); bodies.erase(bodies.begin() + i); finish(c); }
      else i++;
    }
    if (lv.isRemote()) return;
    if ((axleT += dt) >= 0.25) { axleT = 0; axles(); }
    // each driven machine that's free and not where it should be: off it goes
    for (auto it = driven.begin(); it != driven.end();) {
      const int realm = it->first; const ivec3 p(it->second[0], it->second[1], it->second[2]);
      World* w = lv.worldIfOpen(realm);
      if (!w || !w->isLoaded(p.x, p.y, p.z) || !isMachine(w->getBlock(p.x, p.y, p.z))) { it = driven.erase(it); continue; }
      if (moving(realm, p)) { ++it; continue; }
      json& st = machineState(*w, p);
      const float speed = st.value("speed", 1.f);
      if (st.contains("goto")) { start(realm, p, nullptr, speed); st.erase("goto"); ++it; continue; }   // (a lift called while it was moving)
      if (st.value("spin", false)) { std::string why; if (!start(realm, p, &why, speed, true)) { DQ_LOG("moving: spin %s", why.c_str()); st["spin"] = false; } ++it; continue; }
      if (!st.contains("want") || st["want"].get<bool>() == st.value("open", false)) { it = driven.erase(it); continue; }
      // blocked (someone in the way, the other leaf still swinging): it tries again every half second for 10 s, then gives
      // up until told again
      if (clock < st.value("wantNext", 0.0)) { ++it; continue; }
      if (!start(realm, p, nullptr, speed)) {
        const int tries = st.value("wantTries", 0) + 1;
        if (tries >= 20) { st.erase("want"); st.erase("wantTries"); st.erase("wantNext"); }
        else { st["wantTries"] = tries; st["wantNext"] = clock + 0.5; }
      } else { st.erase("wantTries"); st.erase("wantNext"); }
      ++it;
    }
  }

  // §3 mill machinery: an axle ending at a hinge or slider drives it while it turns
  void axles() {
    std::set<std::pair<int, std::array<int, 3>>> now;
    for (auto& [r, wp] : lv.allRealms()) {
      Mechanics* mc = mechanicsOf(lv, r); if (!mc) continue;
      World& w = *wp;
      for (auto& [k, n] : mc->nodes()) {
        if (n.kind != "axle") continue;
        const ivec3 d = n.axis == 0 ? ivec3(0, 1, 0) : n.axis == 1 ? ivec3(1, 0, 0) : ivec3(0, 0, 1);
        for (int sgn : {1, -1}) {
          const ivec3 q = n.p + d * sgn;
          if (!w.isLoaded(q.x, q.y, q.z) || !isMachine(w.getBlock(q.x, q.y, q.z))) continue;
          const float sp = mc->speedAt(n.p.x, n.p.y, n.p.z);
          json& st = machineState(w, q);
          const bool on = sp > 0.15f;   // (an axle left without its windmill or wheel slows for a while: below this it has stopped)
          st["speed"] = std::max(0.25f, sp);
          if (st.value("quarters", 1) == 0 && reg().block(w.getBlock(q.x, q.y, q.z)).str("machine") == "hinge") st["spin"] = on;
          else { st["want"] = on; st["spin"] = false; }
          now.insert({r, {q.x, q.y, q.z}}); driven.insert({r, {q.x, q.y, q.z}});
        }
      }
    }
    for (auto& a : axled) if (!now.count(a)) {   // its axle gone or stopped turning for good: it shuts
      if (World* w = lv.worldIfOpen(a.first)) { const ivec3 p(a.second[0], a.second[1], a.second[2]);
        if (w->isLoaded(p.x, p.y, p.z) && isMachine(w->getBlock(p.x, p.y, p.z))) { json& st = machineState(*w, p); st["spin"] = false; st["want"] = false; st["speed"] = 1.f; driven.insert(a); } }
    }
    axled = std::move(now);
  }
  void save(Level&) override { finishAllNow(); }

  // a chunk a body touches (where it came from or where it's going) unloading: that body finishes now
  void unloading(World& w, Chunk& c) {
    for (size_t i = 0; i < bodies.size();) {
      bool hit = false;
      for (const BodyBlock& k : bodies[i].blocks) {
        const ivec3 a = bodies[i].m.machine + k.rel, z = movedCell(bodies[i].m, a);
        for (const ivec3& p : {a, z}) if ((p.x >> 4) == c.cx && (p.z >> 4) == c.cz) hit = true;
        if (hit) break;
      }
      if (hit && lv.worldIfOpen(bodies[i].realm) == &w) { Body b = std::move(bodies[i]); bodies.erase(bodies.begin() + i); finish(b); }
      else i++;
    }
  }
  void attachHooks();
private:
  Level& lv;
};

class MovingWorldHook : public WorldSystem {
public:
  explicit MovingWorldHook(MovingBodies* m) : mb(m) {}
  const char* name() const override { return "movingHook"; }
  void onChunkUnloading(World& w, Chunk& c) override { mb->unloading(w, c); }
  MovingBodies* mb;
};

void MovingBodies::attachHooks() {
  for (auto& [r, w] : lv.allRealms()) if (!w->system<MovingWorldHook>()) w->addSystem(std::make_shared<MovingWorldHook>(this));
}

MovingBodies* bodiesOf(Level& lv) {
  for (auto& s : lv.systems) if (auto* p = dynamic_cast<MovingBodies*>(s.get())) return p;
  return nullptr;
}
}

void machineWant(Level& lv, int realm, ivec3 p, bool open, float speed) {
  auto* m = bodiesOf(lv); if (!m) return;
  m->attachHooks();
  World* w = lv.worldIfOpen(realm); if (!w || !isMachine(w->getBlock(p.x, p.y, p.z))) return;
  json& st = machineState(*w, p); st["want"] = open; st["speed"] = speed;
  m->driven.insert({realm, {p.x, p.y, p.z}});
}
void liftCall(Level& lv, int realm, ivec3 slider, int at) {
  auto* m = bodiesOf(lv); if (!m) return;
  m->attachHooks();
  World* w = lv.worldIfOpen(realm); if (!w) return;
  machineState(*w, slider)["goto"] = at;
  if (!m->moving(realm, slider)) { m->start(realm, slider, nullptr); machineState(*w, slider).erase("goto"); }
  else m->driven.insert({realm, {slider.x, slider.y, slider.z}});
}
bool machineHeld(const World& w, ivec3 p) {
  if (Lift l; liftOfSlider(w, p, l)) return false;   // (a lift goes on by hand whatever the wires say)
  const json* e = const_cast<World&>(w).blockEntity(p.x, p.y, p.z);
  return e && e->is_object() && e->value("rsOn", false) && !e->value("latched", false);
}
int machineSignal(const World& w, ivec3 p) {
  for (MovingBodies* m : MovingBodies::all())
    for (const Body& b : m->bodies) if (b.m.machine == p && m->worldOf(b.realm) == &w) {
      const float f = b.fraction(); const bool opening = machineOpen(w, p);   // (open is set as it sets off)
      return std::clamp((int)std::lround((opening ? f : 1 - f) * 15), 1, 14);
    }
  return machineOpen(w, p) ? 15 : 0;
}
// §3: powered opens, unpowered shuts (a lever holds it); a pulse shorter than 1.5 s (a button) toggles it instead
void machineRedstone(Level& lv, World& w, ivec3 p, bool on, long long now) {
  json& st = machineState(w, p);
  if (on == st.value("rsOn", false)) return;
  st["rsOn"] = on;
  if (Lift l; liftOfSlider(w, p, l)) {   // a lift: each signal sends it on to the next floor
    if (on) liftCall(lv, w.realm, p, liftNext(l, std::clamp(st.value("at", 0), 0, l.maxAt())));
    w.blockEntityChanged(p.x, p.y, p.z);
    return;
  }
  if (on) {
    st["rsT"] = now;
    if (st.value("latched", false)) { st["latched"] = false; machineWant(lv, w.realm, p, false); }
    else machineWant(lv, w.realm, p, true);
  } else if (now - st.value("rsT", now) < 30) st["latched"] = st.value("want", false);   // (a pulse that shut it leaves nothing latched)
  else machineWant(lv, w.realm, p, false);
  w.blockEntityChanged(p.x, p.y, p.z);
}

bool startMove(Level& lv, int realm, ivec3 machine, std::string* why) { auto* m = bodiesOf(lv); if (m) m->attachHooks(); return m && m->start(realm, machine, why); }
void finishAll(Level& lv) { if (auto* m = bodiesOf(lv)) m->finishAllNow(); }
const std::vector<Body>& movingBodiesList(Level& lv) { static const std::vector<Body> none; auto* m = bodiesOf(lv); return m ? m->bodies : none; }
void registerMovingUse();   // moving_use.cpp
void registerMovingBodies() { setExtraBoxes(&MovingBodies::extraBoxes); registerLevelSystem([](Level& lv) { return std::make_shared<MovingBodies>(lv); }); registerMovingUse(); }

} // namespace dq
