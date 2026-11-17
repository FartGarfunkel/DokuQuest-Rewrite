// sim/moving.cpp — see moving.h.
#include "sim/moving.h"
#include "sim/glue.h"
#include "sim/rotate.h"
#include "sim/gameplay.h"
#include "sim/worldsys.h"
#include "sim/machines.h"
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
  return D6F[w.getMeta(p.x, p.y, p.z) % 6];
}

json& machineState(World& w, ivec3 p) {
  json* e = w.blockEntity(p.x, p.y, p.z, true);
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
  explicit MovingBodies(Level& l) : lv(l) { all().push_back(this); }
  ~MovingBodies() override { auto& a = all(); a.erase(std::remove(a.begin(), a.end(), this), a.end()); }
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
    if (m.kind == 0) m.quarters = open ? -q : q;
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
    Body body; body.id = nextId++; body.realm = realm; body.m = m; body.spin = spin;
    body.dur = (m.kind == 1 ? std::abs(m.distance) / 2.f : std::abs(m.quarters) * 1.f) / std::clamp(speed, 0.25f, 2.f);
    for (const ivec3& c : p.cells) {
      const json* e = w.blockEntity(c.x, c.y, c.z);
      body.blocks.push_back({c - machine, w.getBlock(c.x, c.y, c.z), w.getMeta(c.x, c.y, c.z), e ? *e : json(), w.getCarve(c.x, c.y, c.z)});
    }
    { QuietEdits quiet;   // (lifted whole: a door or torch on a lifted plank must not drop as it goes, nor sand fall)
      for (const ivec3& c : p.cells) { w.setBlockEntity(c.x, c.y, c.z, nullptr); w.setBlock(c.x, c.y, c.z, AIR); } }
    if (!spin) st["open"] = !open;
    if (!open && !spin) { st["usedQ"] = q; st["usedD"] = d; }   // (cleared when it's really shut again: finish())
    w.blockEntityChanged(machine.x, machine.y, machine.z);
    sfxAt(lv, m.kind == 1 ? "piston_out" : "door_open", vec3(machine) + vec3(0.5f));
    bodies.push_back(std::move(body));
    return true;
  }

  void finish(Body& b) {
    World* wp = lv.worldIfOpen(b.realm);
    if (!wp) return;
    World& w = *wp;
    QuietEdits quiet;   // (set down whole, the same way)
    auto freeCell = [&](ivec3 p) { const BlockId id = w.getBlock(p.x, p.y, p.z); return w.isLoaded(p.x, p.y, p.z) && (!id || reg().tReplaceable[id] || reg().tLiquid[id]); };
    // all of it where it was going, or (something put in the way since it set off) all of it back: never split
    bool arrive = true;
    for (const BodyBlock& k : b.blocks) if (!freeCell(movedCell(b.m, b.m.machine + k.rel))) { arrive = false; break; }
    if (!arrive && !b.spin) {
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
        dropItem(lv, b.realm, ItemStack(drop, 1), vec3(to) + vec3(0.5f), true);
        if (e.is_object() && e.contains("items") && e["items"].is_array()) for (auto& it : e["items"]) if (!it.is_null()) dropItem(lv, b.realm, ItemStack::fromJson(it), vec3(to) + vec3(0.5f), true);
        continue;
      }
      w.setBlock(place.x, place.y, place.z, k.id, meta);
      if (carve) w.setCarve(place.x, place.y, place.z, carve);
      if (!e.is_null()) w.setBlockEntity(place.x, place.y, place.z, e);
    }
    sfxAt(lv, b.m.kind == 1 ? "piston_in" : "door_close", vec3(b.m.machine) + vec3(0.5f));
  }

  void finishAllNow() { auto bs = std::move(bodies); bodies.clear(); for (Body& b : bs) finish(b); }

  // machines something drives (a signal, an axle): realm and place
  std::set<std::pair<int, std::array<int, 3>>> driven, axled;
  double axleT = 0;
  bool moving(int realm, ivec3 p) const { for (const Body& b : bodies) if (b.realm == realm && b.m.machine == p) return true; return false; }

  void tick(Level&, double dt) override {
    attachHooks();
    for (size_t i = 0; i < bodies.size();) {
      bodies[i].t += (float)dt;
      if (bodies[i].t >= bodies[i].dur) { Body b = std::move(bodies[i]); bodies.erase(bodies.begin() + i); finish(b); }
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
      if (st.value("spin", false)) { std::string why; if (!start(realm, p, &why, speed, true)) { DQ_LOG("moving: spin %s", why.c_str()); st["spin"] = false; } ++it; continue; }
      if (!st.contains("want") || st["want"].get<bool>() == st.value("open", false)) { it = driven.erase(it); continue; }
      if (!start(realm, p, nullptr, speed)) st.erase("want");   // (blocked: it gives up until told again)
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
bool machineHeld(const World& w, ivec3 p) {
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
void registerMovingBodies() { registerLevelSystem([](Level& lv) { return std::make_shared<MovingBodies>(lv); }); registerMovingUse(); }

} // namespace dq
