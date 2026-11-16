// sim/moving.cpp — see moving.h.
#include "sim/moving.h"
#include "sim/glue.h"
#include "sim/rotate.h"
#include "sim/gameplay.h"
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
  explicit MovingBodies(Level& l) : lv(l) {}
  const char* name() const override { return "moving"; }
  std::vector<Body> bodies;
  u32 nextId = 1;

  bool start(int realm, ivec3 machine, std::string* why) {
    World& w = lv.world(realm);
    if (!isMachine(w.getBlock(machine.x, machine.y, machine.z))) { if (why) *why = "not a hinge or a slider"; return false; }
    for (const Body& b : bodies) if (b.realm == realm && b.m.machine == machine) { if (why) *why = "already moving"; return false; }
    json& st = machineState(w, machine);
    const bool open = st.value("open", false);
    Motion m; m.machine = machine; m.face = machineFace(w, machine);
    m.kind = reg().block(w.getBlock(machine.x, machine.y, machine.z)).str("machine") == "slider" ? 1 : 0;
    int q = st.value("quarters", 1); if (q == 0) q = 1;   // (turning for good needs an axle: stage 2; by hand a quarter)
    const int d = std::clamp(st.value("distance", 1), 1, 16);
    if (m.kind == 0) m.quarters = open ? -q : q;
    else { m.distance = open ? -d : d; m.reach = open ? 1 + d : 1; }
    Piece p;
    if (!gatherPiece(w, m, p, why)) return false;
    std::vector<ivec3> breakable; ivec3 at(0);
    if (!pathClear(w, m, p, &breakable, &at)) {
      if (why) *why = "something is in the way";
      lv.fx->particles("burst", vec3(at) + vec3(0.5f), 6, json{{"item", "stone"}, {"speed", 0.2}});
      sfxAt(lv, "door_locked", vec3(machine) + vec3(0.5f));
      return false;
    }
    for (const ivec3& b : breakable) w.setBlock(b.x, b.y, b.z, AIR);
    Body body; body.id = nextId++; body.realm = realm; body.m = m;
    body.dur = m.kind == 1 ? std::abs(m.distance) / 2.f : std::abs(m.quarters) * 1.f;
    for (const ivec3& c : p.cells) {
      const json* e = w.blockEntity(c.x, c.y, c.z);
      body.blocks.push_back({c - machine, w.getBlock(c.x, c.y, c.z), w.getMeta(c.x, c.y, c.z), e ? *e : json(), w.getCarve(c.x, c.y, c.z)});
    }
    for (const ivec3& c : p.cells) { w.setBlockEntity(c.x, c.y, c.z, nullptr); w.setBlock(c.x, c.y, c.z, AIR); }
    st["open"] = !open; w.blockEntityChanged(machine.x, machine.y, machine.z);
    sfxAt(lv, m.kind == 1 ? "piston_out" : "door_open", vec3(machine) + vec3(0.5f));
    bodies.push_back(std::move(body));
    return true;
  }

  void finish(Body& b) {
    World* wp = lv.worldIfOpen(b.realm);
    if (!wp) return;
    World& w = *wp;
    for (BodyBlock& k : b.blocks) {
      const ivec3 from = b.m.machine + k.rel, to = movedCell(b.m, from);
      u8 meta = turnMeta(b.m, k.id, k.meta), carve = turnCarve(b.m, k.carve);
      json e = k.entity;
      if (e.is_object() && e.contains("glue")) {   // the glue's faces turn with it
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

  void tick(Level&, double dt) override {
    attachHooks();
    for (size_t i = 0; i < bodies.size();) {
      bodies[i].t += (float)dt;
      if (bodies[i].t >= bodies[i].dur) { Body b = std::move(bodies[i]); bodies.erase(bodies.begin() + i); finish(b); }
      else i++;
    }
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

bool startMove(Level& lv, int realm, ivec3 machine, std::string* why) { auto* m = bodiesOf(lv); if (m) m->attachHooks(); return m && m->start(realm, machine, why); }
void finishAll(Level& lv) { if (auto* m = bodiesOf(lv)) m->finishAllNow(); }
const std::vector<Body>& movingBodiesList(Level& lv) { static const std::vector<Body> none; auto* m = bodiesOf(lv); return m ? m->bodies : none; }
void registerMovingUse();   // moving_use.cpp
void registerMovingBodies() { registerLevelSystem([](Level& lv) { return std::make_shared<MovingBodies>(lv); }); registerMovingUse(); }

} // namespace dq
