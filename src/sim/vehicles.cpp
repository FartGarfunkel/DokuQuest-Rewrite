// sim/vehicles.cpp — rowboats, sailboats and the sailing ship (game/vehicles.js, boats2.js, ships.js,
// lilypads.js), the wind, and the riding API shared with minecarts.
#include "sim/vehicles.h"
#include "sim/minecart.h"
#include "sim/redstone.h"
#include "sim/player.h"
#include "sim/physics.h"
#include <random>

namespace dq {
using namespace rs;

void Wind::update(float dt) {
  t += dt;
  dir += (std::sin(t * 0.013f) * 0.6f + std::sin(t * 0.0071f + 2) * 0.4f) * dt * 0.02f;
  gust += ((std::sin(t * 0.37f) * std::sin(t * 0.11f + 1) > 0.55f ? 0.35f : 0.f) - gust) * std::min(1.f, dt * 0.8f);
  strength = clampv(0.45f + std::sin(t * 0.021f) * 0.2f + std::sin(t * 0.0057f + 3) * 0.15f + gust, 0.15f, 1.f);
}

static const BoatType BOATS[] = {
  {"rowboat", "rowboat", 3, 1.f, false, {vec3(0, 0.25f, 0.25f), vec3(0, 0.25f, -0.55f)}},
  {"sailboat", "sailboat", 4, 1.6f, false, {vec3(0, 0.42f, 1.38f), vec3(0, 0.42f, -1.15f)}},
  {"ship", "sailing_ship", 14, 1.f, true, {vec3(0, 1.3f, 3.75f), vec3(0.9f, 1.35f, -1.4f)}},
};
const BoatType& boatType(const std::string& k) { for (auto& b : BOATS) if (k == b.key) return b; return BOATS[0]; }
float shipHalfAt(float lz) { return lz < SHIP.bow + 2.4f ? SHIP.halfW * clampv((lz - SHIP.bow) / 2.4f, 0.15f, 1.f) : SHIP.halfW; }
bool onShipDeck(float lx, float lz, float inset) { return lz > SHIP.bow + 0.2f + inset && lz < SHIP.stern - inset && std::fabs(lx) < shipHalfAt(lz) - inset; }

void vehToast(Level& lv, const Player& p, const std::string& text, const std::string& kind) { lv.fx->message(p.id, text, kind); }
bool waterAt(const World& w, int x, int y, int z) {
  BlockId id = w.getBlock(x, y, z);
  if (reg().tLiquid[id] == 1) return true;
  return reg().tWlog[id] && (w.getMeta(x, y, z) & 0x80) != 0;
}
void moveVehicle(Level& lv, Entity& e, double dt, bool* hitWall) {
  World& w = lv.world(e.realm);
  MoveResult r = moveBox(w, e.pos, e.w, e.h, e.vel * (float)dt, 0);
  if (r.hitX) e.vel.x = 0;
  if (r.hitZ) e.vel.z = 0;
  if (r.hitY) e.vel.y = 0;
  e.onGround = r.onGround;
  if (hitWall) *hitWall = r.hitX || r.hitZ;
}

// ---------------------------------------------------------------------------------------- shared vehicle bits
bool isVehicle(const Entity& e) { return dynamic_cast<const Vehicle*>(&e) != nullptr; }
Vehicle* asVehicle(Entity* e) { return e ? dynamic_cast<Vehicle*>(e) : nullptr; }
void Vehicle::ensureUid() {
  if (!uid.empty()) return;
  static std::mt19937_64 g{std::random_device{}()};
  char b[24]; snprintf(b, sizeof b, "%016llx", (unsigned long long)g());
  uid = b;
}
json Vehicle::saveCommon() const { json d = json::object(); d["uid"] = uid; d["health"] = health; return d; }
void Vehicle::loadCommon(const json& d) { uid = d.value("uid", ""); health = d.value("health", health); ensureUid(); }
void Vehicle::seatPet(Level& lv, vec3 at) {
  if (!pet && age < 1.5f && !uid.empty()) {   // after loading: find the pet that rides here
    for (auto& e : lv.entities()) if (!e->removed && e.get() != this && e->data.value("mountedOn", "") == uid) { pet = e->id; break; }
  }
  if (!pet) return;
  Entity* m = lv.entity(pet);
  if (!m || m->removed) { pet = 0; return; }
  m->pos = at; m->vel = vec3(0); m->riding = id; m->yaw = yaw; m->fallDist = 0;
}
void Vehicle::tickCommon(Level& lv, double dt) {
  float d = (float)dt;
  if (hitT > 0) hitT -= d;
  if (shakeT > 0) shakeT -= d;
  for (auto& r : riders) {
    if (!r) continue;
    auto* p = dynamic_cast<Player*>(lv.entity(r));
    if (!p || p->removed || p->dead || p->riding != id || p->realm != realm) { if (p && p->riding == id) p->riding = 0; r = 0; }
  }
  VehicleSystem* vs = vehicleSystem(lv);
  if (vs && inpT >= 0 && vs->time - inpT > 0.3) { inp = VehicleInput(); inpT = -1; }
  // a hit from the combat system shows as an hp drop: count it as one knock
  if (lastHp < 0) lastHp = hp;
  else if (hp < lastHp - 0.01f) {
    hp = lastHp;
    Player* near = nullptr; float best = 6;
    for (auto& e : lv.entities()) if (e->isPlayer() && !e->removed && e->realm == realm) { float dd = glm::distance(e->pos, pos); if (dd < best) { best = dd; near = static_cast<Player*>(e.get()); } }
    hurt(lv, near, false);
  }
}

// ---------------------------------------------------------------------------------------- boats
vec3 Boat::seatPos(int i) const {
  vec3 s = T().seats[clampv(i, 0, 1)];
  float c = std::cos(yaw), sn = std::sin(yaw);
  return vec3(pos.x + s.x * c + s.z * sn, pos.y + s.y, pos.z - s.x * sn + s.z * c);
}
float Boat::waterSurface(const World& w) const {
  int x = ifloor(pos.x), z = ifloor(pos.z), y0 = ifloor(pos.y + 0.5f);
  for (int y = y0 + 1; y >= y0 - 2; y--) if (waterAt(w, x, y, z) && !waterAt(w, x, y + 1, z)) return y + 0.88f;
  return NAN;
}
bool Boat::mount(Level& lv, Player& p) {
  VehicleSystem* vs = vehicleSystem(lv);
  auto& st = vs->ps(p.id);
  if (ship()) {
    // from outside you climb aboard; on deck you take the wheel
    if (st.aboard != id) {
      vec2 l = toLocal(p.pos.x, p.pos.z);
      float lz = clampv(l.y, SHIP.bow + 1.2f, SHIP.stern - 0.6f), hw = shipHalfAt(lz) - 0.5f, lx = clampv(l.x, -hw, hw);
      vec2 wp = toWorld(lx, lz);
      p.pos = vec3(wp.x, pos.y + SHIP.deck, wp.y); p.vel = vec3(0); p.fallDist = 0; p.onGround = true;
      st.aboard = id; st.shipPose = vec4(pos, yaw);
      vehToast(lv, p, "Aboard. Use the wheel at the back to sail");
      lv.fx->material("wood", "step", p.pos);
      return true;
    }
    if (riders[0]) { vehToast(lv, p, "Someone has the wheel"); return false; }
    riders[0] = p.id; p.riding = id; p.yaw = yaw; st.yawRef = yaw; st.yawSet = true;
    vehToast(lv, p, "W/S set the sail \xC2\xB7 A/D steer \xC2\xB7 sneak to let go of the wheel");
    lv.fx->soundUi(p.id, "door_close");
    return true;
  }
  bool loaded = pet || !cargo.is_null() || items.is_array();
  if (loaded && riders[0]) { vehToast(lv, p, "The boat is full"); return false; }
  int i = riders[0] ? (riders[1] || loaded ? -1 : 1) : 0;
  if (i < 0) { vehToast(lv, p, "The boat is full"); return false; }
  riders[i] = p.id; p.riding = id; p.vel = vec3(0);
  st.yawSet = false; st.onBoat = 0;
  vehToast(lv, p, kind == "sailboat" && i == 0 ? "W/S set the sail \xC2\xB7 A/D steer \xC2\xB7 sneak to get out" : i == 0 ? "W/S row \xC2\xB7 A/D steer \xC2\xB7 sneak to get out" : "Sneak to get out");
  lv.fx->soundUi(p.id, "door_close");
  return true;
}
void Boat::dismount(Level& lv, Player& p) {
  int i = seatOf(p.id);
  if (i < 0) return;
  riders[i] = 0; p.riding = 0;
  auto& st = vehicleSystem(lv)->ps(p.id);
  st.yawSet = false;
  if (ship()) {
    // let go of the wheel and stay on deck
    vec2 wp = toWorld(0, SHIP.helm.y + 0.2f);
    p.pos = vec3(wp.x, pos.y + SHIP.deck, wp.y); p.vel.y = 0; p.fallDist = 0;
    st.aboard = id; st.shipPose = vec4(pos, yaw);
    return;
  }
  // getting out leaves you standing on it (boats2.js)
  vec3 s = seatPos(i);
  p.pos = vec3(s.x, pos.y + SMALL_BOAT.top * scale() + 0.01f, s.z); p.vel = vec3(0); p.fallDist = 0;
  st.onBoat = id; st.boatPose = vec4(pos, yaw);
}
bool Boat::use(Level& lv, Player& p, ItemStack* held) {
  if (p.sneaking || p.riding) return false;
  (void)held;
  mount(lv, p);
  return true;
}
void Boat::hurt(Level& lv, Player* by, bool blast) {
  if (ship() && !blast) {
    if (!by) return;
    if (!by->sneaking) { vehToast(lv, *by, "Sneak and hit it to break up the ship"); return; }
  }
  if (hitT > 0 && !blast) return;
  hitT = 0.3f; health -= blast ? 3 : 1; shakeT = 0.3f;
  lv.fx->sound("hit", pos);
  if (health <= 0) breakApart(lv, by ? by->creative() : false);
}
void Boat::breakApart(Level& lv, bool) {
  if (removed) return;
  VehicleSystem* vs = vehicleSystem(lv);
  for (auto& [pid, st] : vs->players) { if (st.aboard == id) st.aboard = 0; if (st.onBoat == id) st.onBoat = 0; }
  vec3 at = pos + vec3(0, 0.8f, 0);
  auto scatter = [] { return vec3((rnd() - 0.5f) * 2, 2.5f, (rnd() - 0.5f) * 2); };
  if (items.is_array()) {
    for (auto& j : items) { ItemStack s = ItemStack::fromJson(j); if (!s.empty()) dropStack(lv, realm, s, at, scatter()); }
    dropStack(lv, realm, ItemStack(cargoKey.empty() ? "chest" : cargoKey, 1), at, scatter());
  }
  if (!cargo.is_null()) {
    dropStack(lv, realm, ItemStack(cargo.value("key", "stone"), 1), at, scatter());
    if (cargo["ent"].is_object() && cargo["ent"]["items"].is_array()) for (auto& j : cargo["ent"]["items"]) { ItemStack s = ItemStack::fromJson(j); if (!s.empty()) dropStack(lv, realm, s, at, scatter()); }
  }
  items = nullptr; cargo = nullptr;
  if (pet) { if (Entity* m = lv.entity(pet)) { m->riding = 0; m->data.erase("mountedOn"); m->pos.y += 0.6f; } pet = 0; }
  for (u32 r : riders) if (r) if (auto* p = dynamic_cast<Player*>(lv.entity(r))) { int i = seatOf(p->id); riders[i] = 0; p->riding = 0; p->pos.y += 1; }
  removed = true;
  dropStack(lv, realm, ItemStack(T().item, 1), pos + vec3(0, 0.5f, 0), scatter());
}
bool Boat::pick(vec3 eye, vec3 dir, float& t) const {
  vec2 o = toLocal(eye.x, eye.z);
  float c = std::cos(yaw), n = std::sin(yaw);
  vec3 ld(dir.x * c - dir.z * n, dir.y, dir.x * n + dir.z * c), lo(o.x, eye.y - pos.y, o.y);
  vec3 bmin, bmax;
  if (ship()) { bmin = vec3(-SHIP.halfW - 0.15f, 0, SHIP.bow - 0.45f); bmax = vec3(SHIP.halfW + 0.15f, SHIP.deck + 1.2f, SHIP.stern + 0.3f); }
  else { float sc = scale(); bmin = vec3(-0.72f, 0, SMALL_BOAT.bow - 0.05f) * sc; bmax = vec3(0.72f, 0.62f, SMALL_BOAT.stern + 0.07f) * sc; }
  float t0 = 0, t1 = 1e9f;
  for (int a = 0; a < 3; a++) {
    if (std::fabs(ld[a]) < 1e-6f) { if (lo[a] < bmin[a] || lo[a] > bmax[a]) return false; continue; }
    float ta = (bmin[a] - lo[a]) / ld[a], tb = (bmax[a] - lo[a]) / ld[a];
    if (ta > tb) std::swap(ta, tb);
    t0 = std::max(t0, ta); t1 = std::min(t1, tb);
    if (t0 > t1) return false;
  }
  t = t0; return true;
}
bool Boat::loadCargo(Level& lv, const json& c) {
  if (removed || pet || !cargo.is_null() || items.is_array()) return false;
  if (!ship() && riders[1]) return false;
  if (c.value("kind", "") == "mob") {
    if (ship() || riders[1]) return false;
    Entity* m = lv.entity(c.value("id", 0u));
    if (!m) return false;
    ensureUid(); pet = m->id; m->data["mountedOn"] = uid; m->riding = id;
    return true;
  }
  std::string key = c.value("key", "");
  if (key == "chest" || key == "barrel" || key == "trapped_chest") {
    items = json::array(); for (int i = 0; i < 27; i++) items.push_back(nullptr);
    cargoKey = key;
    if (c.contains("ent") && c["ent"].is_object() && c["ent"]["items"].is_array()) { const json& s = c["ent"]["items"]; for (size_t i = 0; i < s.size() && i < 27; i++) items[i] = s[i]; }
  } else cargo = json{{"key", key}, {"meta", c.value("meta", 0)}, {"ent", c.contains("ent") ? c["ent"] : json(nullptr)}};
  lv.fx->sound("chest", pos);
  return true;
}
json Boat::unloadCargo(Level& lv) {
  if (pet) { u32 m = pet; pet = 0; if (Entity* e = lv.entity(m)) { e->riding = 0; e->data.erase("mountedOn"); } return json{{"kind", "mob"}, {"id", m}}; }
  if (!cargo.is_null()) { json c = cargo; cargo = nullptr; c["kind"] = "block"; return c; }
  if (items.is_array()) {
    json c{{"kind", "block"}, {"key", cargoKey.empty() ? "chest" : cargoKey}, {"meta", 0}, {"ent", json{{"type", "chest"}, {"items", items}}}};
    items = nullptr; cargoKey.clear();
    return c;
  }
  return nullptr;
}

void Boat::tick(Level& lv, double dtd) {
  if (lv.isRemote()) return;
  float dt = (float)dtd;
  World& w = lv.world(realm);
  const BoatType& BT = T();
  if (BT.ship) { this->w = 1.3f; h = 1.6f; heel *= 0.6f; }
  else if (BT.scale != 1 && !scaled) { scaled = true; this->w = 0.65f * BT.scale; }
  // (lilypads.js) a hull pushes through lily pads, breaking them off
  if (BlockId lily = rs::T().lily) {
    float r = (BT.ship ? 1.6f : this->w) + 0.35f, sp = std::min(3.f, std::hypot(vel.x, vel.z) * 0.25f);
    float cx = pos.x + vel.x * 0.25f, cz = pos.z + vel.z * 0.25f;
    for (int y = ifloor(pos.y - 0.6f); y <= ifloor(pos.y + 0.9f); y++)
      for (int x = ifloor(cx - r - sp); x <= ifloor(cx + r + sp); x++) for (int z = ifloor(cz - r - sp); z <= ifloor(cz + r + sp); z++) {
        if (w.getBlock(x, y, z) != lily) continue;
        w.setBlock(x, y, z, AIR, 0);
        if (rnd() < 0.5f) dropStack(lv, realm, ItemStack("lily_pad", 1), vec3(x + 0.5f, y + 0.3f, z + 0.5f), vec3(0, 1, 0));
      }
  }
  tickCommon(lv, dtd);
  if (removed) return;
  bob += dt;
  float surf = waterSurface(w);
  bool floating = !std::isnan(surf) && pos.y < surf + 0.3f;
  // the helmsman's controls
  bool helm = riders[0] != 0;
  float steer = helm ? -inp.mx : 0;
  turn += (steer * (floating ? 1.6f : 0.6f) - turn) * std::min(1.f, dt * 4);
  yaw += turn * dt * (0.4f + std::min(1.f, std::fabs(speed) / 3));
  float target = 0;
  if (kind == "rowboat") {
    if (helm) target = inp.mz < -0.3f ? 4.6f : inp.mz > 0.3f ? -2.f : 0.f;
    if (!floating) target *= 0.15f;
  } else {
    if (helm) sail = clampv(sail + (inp.mz < -0.3f ? 0.7f : inp.mz > 0.3f ? -0.7f : 0.f) * dt, 0.f, 1.f);
    Wind& wind = windOf(lv);
    float hx = -std::sin(yaw), hz = -std::cos(yaw), wx = -std::sin(wind.dir), wz = -std::cos(wind.dir);   // where the wind blows to
    float th = std::acos(clampv(hx * wx + hz * wz, -1.f, 1.f));                                         // 0 = wind straight behind
    const float P = PI;
    eff = th < P / 2 ? lerpf(0.72f, 1, th / (P / 2)) : th < 0.78f * P ? lerpf(1, 0.42f, (th - P / 2) / (0.28f * P)) : std::max(0.f, lerpf(0.42f, 0, (th - 0.78f * P) / (0.1f * P)));
    target = floating ? sail * wind.strength * eff * 10 : 0;
    // the boom swings out to leeward, more the further off the wind
    float cr = hx * wz - hz * wx, side = cr > 0 ? 1.f : cr < 0 ? -1.f : 1.f;
    float wantBoom = side * clampv((P - th) * 0.55f, 0.12f, 1.35f);
    boom += (wantBoom - boom) * std::min(1.f, dt * 2);
    float wantHeel = floating ? -side * sail * eff * wind.strength * 0.18f * clampv(th / (P / 2), 0.f, 1.f) * (th < P / 2 ? 1 : 1.3f) : 0;
    heel += (wantHeel - heel) * std::min(1.f, dt * 1.5f);
  }
  float accel = target > speed ? 1.2f : 2.2f;
  speed += (target - speed) * std::min(1.f, dt * accel);
  if (!floating && std::fabs(speed) > 0.5f) speed *= std::max(0.f, 1 - dt * 4);
  vel.x = -std::sin(yaw) * speed; vel.z = -std::cos(yaw) * speed;
  // float
  if (!std::isnan(surf) && pos.y < surf + 0.4f) {
    float want = surf - 0.32f + std::sin(bob * 1.7f) * 0.03f;
    vel.y += (want - pos.y) * 18 * dt;
    vel.y *= std::max(0.f, 1 - dt * 5);
  } else vel.y = std::max(-30.f, vel.y - 22 * dt);
  moveVehicle(lv, *this, dt, &hitWall);
  if (hitWall) speed *= 0.4f;
  if (floating && std::fabs(speed) > 2 && rnd() < dt * 8) {
    vec3 back = seatPos(0);
    lv.fx->particles("splash", vec3(back.x + (rnd() - 0.5f) * 0.6f, surf + 0.02f, back.z + (rnd() - 0.5f) * 0.6f), 1);
  }
  // carry the riders
  VehicleSystem* vs = vehicleSystem(lv);
  for (int i = 0; i < 2; i++) {
    if (!riders[i]) continue;
    auto* p = dynamic_cast<Player*>(lv.entity(riders[i]));
    if (!p) continue;
    p->pos = seatPos(i); p->vel = vec3(vel.x, 0, vel.z); p->fallDist = 0; p->onGround = true;
    auto& st = vs->ps(p->id);
    if (st.yawSet) p->yaw += yaw - st.yawRef;
    st.yawRef = yaw; st.yawSet = true;
  }
  seatPet(lv, seatPos(1) + vec3(0, 0.1f, 0));
  if (pos.y < -40) removed = true;
}
json Boat::save() const {
  json j = Entity::save();
  json& d = j["data"];
  json common = saveCommon();
  for (auto& [k, v] : common.items()) d[k] = v;
  d["kind"] = kind; d["sail"] = sail;
  if (items.is_array()) { d["items"] = items; d["ck"] = cargoKey; } else { d.erase("items"); d.erase("ck"); }
  if (!cargo.is_null()) d["cg"] = cargo; else d.erase("cg");
  return j;
}
void Boat::load(const json& j) {
  Entity::load(j);
  type = "boat";
  kind = boatType(data.value("kind", "rowboat")).key;
  health = (float)T().hp;
  loadCommon(data);
  sail = data.value("sail", 0.6f);
  if (data["items"].is_array()) { items = data["items"]; while (items.size() < 27) items.push_back(nullptr); cargoKey = data.value("ck", "chest"); }
  if (data.contains("cg")) cargo = data["cg"];
  bob = rnd() * 10;
}
Entity* spawnBoat(Level& lv, int realm, const std::string& kind, vec3 at, float yaw) {
  auto b = std::make_unique<Boat>();
  b->kind = boatType(kind).key; b->realm = realm; b->pos = at; b->yaw = yaw;
  b->health = (float)b->T().hp; b->bob = rnd() * 10;
  b->ensureUid();
  return lv.spawn(std::move(b));
}
bool placeBoatItem(Level& lv, Player& p, ItemStack& held) {
  const ItemDef* def = held.def();
  if (!def) return false;
  World& w = lv.world(p.realm);
  vec3 dir(-std::sin(p.yaw) * std::cos(p.pitch), std::sin(p.pitch), -std::cos(p.yaw) * std::cos(p.pitch));
  glm::dvec3 eye = glm::dvec3(p.pos) + glm::dvec3(0, 1.62, 0);
  RayHit hit = raycastBlocks(w, eye, dir, 5.f, true);
  if (!hit.hit) return false;
  bool water = reg().tLiquid[hit.id] == 1;
  if (!water && !(hit.normal.y == 1 && reg().tSolid[hit.id])) { vehToast(lv, p, "Set a boat on water or on the shore"); return false; }
  float t = hit.dist;
  vec3 at((float)eye.x + dir.x * t, water ? hit.block.y + 0.6f : hit.block.y + 1.05f, (float)eye.z + dir.z * t);
  spawnBoat(lv, p.realm, def->str("boat"), at, p.yaw);
  held.n--; if (held.n <= 0) held.clear();
  lv.fx->soundUi(p.id, "splash");
  return true;
}

// ---------------------------------------------------------------------------------------- the riding API
VehicleSystem* vehicleSystem(Level& lv) { return lv.system<VehicleSystem>(); }
Wind& windOf(Level& lv) { static Wind fallback; auto* s = vehicleSystem(lv); return s ? s->wind : fallback; }
// trains are stepped after the entities moved (the level's "tick" event comes after them), like updateEntities
VehicleSystem::VehicleSystem(Level& l) : lv(l) {
  if (lv.events) tickHandler = lv.events->on("tick", [this](const json& a) { if (!lv.isRemote()) trainStep(lv, a.is_array() && !a.empty() && a[0].is_number() ? a[0].get<double>() : TICK_DT); return true; });
}
VehicleSystem::~VehicleSystem() { if (lv.events && tickHandler) lv.events->off(tickHandler); }
void VehicleSystem::tick(Level& l, double dt) {
  time += dt;
  wind.update((float)dt);
  if (!l.isRemote() && !l.events) trainStep(l, dt);
}
Vehicle* ridingOf(Level& lv, Player& p) {
  if (!p.riding) return nullptr;
  Vehicle* v = asVehicle(lv.entity(p.riding));
  if (!v || v->removed || v->seatOf(p.id) < 0) { if (!v || v->removed || !isVehicle(*v)) p.riding = 0; return v && !v->removed && v->seatOf(p.id) >= 0 ? v : nullptr; }
  return v;
}
bool vehicleInput(Level& lv, Player& p, vec2 move, bool jump, bool sneak) {
  VehicleSystem* vs = vehicleSystem(lv);
  if (!vs) return false;
  auto& st = vs->ps(p.id);
  st.inputT = vs->time;
  Vehicle* v = ridingOf(lv, p);
  if (!v) { st.sneakWas = sneak; return false; }
  if (v->seatOf(p.id) == 0) { v->inp.mx = move.x; v->inp.mz = -move.y; v->inp.jump = jump; v->inpT = vs->time; }
  if (sneak && !st.sneakWas) { st.sneakWas = true; v->dismount(lv, p); return true; }
  st.sneakWas = sneak;
  return true;
}
void vehiclesAfterPlayerMove(Level& lv, Player& p, double dt) {
  VehicleSystem* vs = vehicleSystem(lv);
  if (!vs) return;
  auto& st = vs->ps(p.id);
  if (Vehicle* v = ridingOf(lv, p)) {
    // the rider sits in their seat (the vehicle moves on ticks; this keeps the camera steady between them)
    p.pos = v->riderPos(v->seatOf(p.id)); p.vel.y = 0; p.fallDist = 0; p.onGround = true;
    st.onBoat = st.aboard = 0;
    return;
  }
  if (p.dead || p.flying) { st.onBoat = st.aboard = 0; return; }
  // the boat or ship under you moved and turned since last time: so did you
  auto carry = [&](u32 vid, vec4& pose) {
    Boat* s = dynamic_cast<Boat*>(lv.entity(vid));
    if (!s || s->removed) return;
    float dyaw = s->yaw - pose.w, rx = p.pos.x - pose.x, rz = p.pos.z - pose.z, c = std::cos(dyaw), n = std::sin(dyaw);
    p.pos.x = s->pos.x + rx * c + rz * n; p.pos.z = s->pos.z - rx * n + rz * c; p.pos.y += s->pos.y - pose.y; p.yaw += dyaw;
  };
  if (st.onBoat) carry(st.onBoat, st.boatPose);
  if (st.aboard) carry(st.aboard, st.shipPose);
  u32 onBoat = 0, aboard = 0;
  for (auto& ep : lv.entities()) {
    Boat* e = dynamic_cast<Boat*>(ep.get());
    if (!e || e->removed || e->realm != p.realm) continue;
    if (!e->ship()) {
      // (boats2.js) standing on a boat, and bumping into one
      if (std::fabs(e->pos.x - p.pos.x) > 4 || std::fabs(e->pos.z - p.pos.z) > 4) continue;
      float sc = e->scale();
      vec2 l0 = e->toLocal(p.pos.x, p.pos.z);
      float lx = l0.x / sc, lz = l0.y / sc, top = e->pos.y + SMALL_BOAT.top * sc;
      float cl = clampv(lz, SMALL_BOAT.bow, SMALL_BOAT.stern);
      auto inside = [&](float m) { return lz > SMALL_BOAT.bow - m && lz < SMALL_BOAT.stern + m && std::fabs(lx) < SMALL_BOAT.half(cl) + m; };
      if (inside(0.1f) && p.pos.y >= top - 0.45f && p.pos.y <= top + 0.35f && p.vel.y <= 0.5f) { p.pos.y = top; p.vel.y = 0; p.onGround = true; p.fallDist = 0; onBoat = e->id; continue; }
      if (inside(0.3f) && p.pos.y < top - 0.45f && p.pos.y + 1.7f > e->pos.y) {
        float hw = SMALL_BOAT.half(cl) + 0.3f, dSide = hw - std::fabs(lx), dBow = lz - (SMALL_BOAT.bow - 0.3f), dStern = SMALL_BOAT.stern + 0.3f - lz;
        float nlx = lx, nlz = lz;
        if (dSide <= dBow && dSide <= dStern) nlx = (lx < 0 ? -1.f : 1.f) * hw; else if (dBow < dStern) nlz = SMALL_BOAT.bow - 0.3f; else nlz = SMALL_BOAT.stern + 0.3f;
        vec2 wp = e->toWorld(nlx * sc, nlz * sc);
        float ox = wp.x - p.pos.x, oz = wp.y - p.pos.z;
        bool floats = !e->ghost && !std::isnan(e->waterSurface(lv.world(e->realm))) && !e->hasRider();
        float k = floats ? 0.7f : 1.f;
        p.pos.x += ox * k; p.pos.z += oz * k;
        if (floats) { e->pos.x -= ox * 0.3f; e->pos.z -= oz * 0.3f; }
      }
      continue;
    }
    // the ship
    if (std::fabs(e->pos.x - p.pos.x) > 7 || std::fabs(e->pos.z - p.pos.z) > 7) continue;
    float deck = e->pos.y + SHIP.deck;
    vec2 l = e->toLocal(p.pos.x, p.pos.z);
    if (st.aboard != e->id && !(p.pos.y >= deck - 0.4f || p.pos.y + 1.7f < e->pos.y - 0.2f)) {
      // its side stops you (from the water or the shore)
      float m = 0.3f, cz = clampv(l.y, SHIP.bow, SHIP.stern), hw = shipHalfAt(cz) + m;
      if (!(l.y < SHIP.bow - m || l.y > SHIP.stern + m || std::fabs(l.x) > hw)) {
        float dSide = hw - std::fabs(l.x), dBow = l.y - (SHIP.bow - m), dStern = SHIP.stern + m - l.y, nlx = l.x, nlz = l.y;
        if (dSide <= dBow && dSide <= dStern) nlx = (l.x < 0 ? -1.f : 1.f) * hw; else if (dBow < dStern) nlz = SHIP.bow - m; else nlz = SHIP.stern + m;
        vec2 wp = e->toWorld(nlx, nlz); p.pos.x = wp.x; p.pos.z = wp.y;
        continue;
      }
    }
    // the deck holds you up; the rails keep you on unless you jump them
    if (!onShipDeck(l.x, l.y)) continue;
    if (p.pos.y < deck - 0.8f || p.pos.y > deck + 0.35f || p.vel.y > 0.5f) { if (st.aboard == e->id && p.pos.y >= deck - 0.8f && p.pos.y < deck + 1.2f) aboard = e->id; continue; }
    p.pos.y = deck; p.vel.y = 0; p.onGround = true; p.fallDist = 0; aboard = e->id;
  }
  if (Boat* on = dynamic_cast<Boat*>(lv.entity(aboard))) {
    vec2 l = on->toLocal(p.pos.x, p.pos.z);
    float inset = 0.3f;
    if (p.pos.y < on->pos.y + SHIP.deck + 0.6f && !onShipDeck(l.x, l.y, inset)) {
      float cz = clampv(l.y, SHIP.bow + 0.2f + inset, SHIP.stern - inset), hw = shipHalfAt(cz) - inset, cx = clampv(l.x, -hw, hw);
      vec2 wp = on->toWorld(cx, cz); p.pos.x = wp.x; p.pos.z = wp.y;
    }
    st.shipPose = vec4(on->pos, on->yaw);
  }
  if (Boat* on = dynamic_cast<Boat*>(lv.entity(onBoat))) st.boatPose = vec4(on->pos, on->yaw);
  st.onBoat = onBoat; st.aboard = aboard;
  (void)dt;
}
Vehicle* vehiclePick(Level& lv, Player& p, float maxDist, float* dist) {
  VehicleSystem* vs = vehicleSystem(lv);
  vec3 dir(-std::sin(p.yaw) * std::cos(p.pitch), std::sin(p.pitch), -std::cos(p.yaw) * std::cos(p.pitch));
  vec3 eye = p.pos + vec3(0, p.sneaking ? 1.5f : 1.62f, 0);
  Vehicle* best = nullptr; float bt = maxDist;
  for (auto& e : lv.entities()) {
    Vehicle* v = asVehicle(e.get());
    if (!v || v->removed || v->realm != p.realm || v->id == p.riding || glm::distance(v->pos, eye) > maxDist + 10) continue;
    if (vs && vs->ps(p.id).aboard == v->id) continue;   // the ship you stand on isn't something you point at
    float t;
    if (v->pick(eye, dir, t) && t <= bt) { bt = t; best = v; }
  }
  if (dist) *dist = bt;
  return best;
}
bool useVehicle(Level& lv, Player& p, Vehicle& v, ItemStack* held) {
  if (v.removed) return false;
  if (p.sneaking) return false;
  return v.use(lv, p, held);
}
void hurtVehicle(Level& lv, Entity& e, Player* by, bool blast) { if (Vehicle* v = asVehicle(&e)) if (!v->removed) v->hurt(lv, by, blast); }
bool loadVehicle(Level& lv, Vehicle& v, const json& carried) { return v.loadCargo(lv, carried); }
json unloadVehicle(Level& lv, Vehicle& v) { return v.unloadCargo(lv); }

} // namespace dq
