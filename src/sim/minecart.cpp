// sim/minecart.cpp — rails and minecarts (game/minecart.js), trains and the furnace double cart (game/trains.js).
#include "sim/minecart.h"
#include "sim/redstone.h"
#include "sim/redstone_entities.h"
#include "sim/player.h"
#include "sim/physics.h"

namespace dq {
using namespace rs;

static const CartType CARTS[] = {
  {"minecart", "minecart", nullptr, 0, true}, {"chest", "chest_minecart", "chest", 27, false}, {"hopper", "hopper_minecart", "hopper", 5, false},
  {"furnace", "furnace_minecart", "furnace", 0, true}, {"tnt", "tnt_minecart", "tnt", 0, false},
};
const CartType& cartType(const std::string& k) { for (auto& c : CARTS) if (k == c.key) return c; return CARTS[0]; }

void Minecart::become(const std::string& k, const std::string& blockKey) {
  kind = cartType(k).key;
  cargoBlock = blockKey.empty() || (T().block && blockKey == T().block) ? "" : blockKey;
  if (T().slots) { json a = json::array(); for (int i = 0; i < T().slots; i++) a.push_back(nullptr); data["items"] = a; }
  else data.erase("items");
  fuel = 0; fuse = -1; locked = false;
}
void Minecart::prime(Level& lv) { if (fuse < 0) { fuse = 4; lv.fx->sound("fuse", pos); } }

// ---------------------------------------------------------------------------------------- getting in and out
bool Minecart::mount(Level& lv, Player& p) {
  if (pet || !cargo.is_null()) { vehToast(lv, p, pet ? "There's a pet in this cart" : "This cart is loaded"); return false; }
  if (!T().seat) return false;
  if (riders[0]) { vehToast(lv, p, "Someone is already riding"); return false; }
  riders[0] = p.id; p.riding = id; p.vel = vec3(0);
  vehToast(lv, p, "W to push off \xC2\xB7 sneak to get out");
  lv.fx->soundUi(p.id, "door_close");
  return true;
}
void Minecart::dismount(Level& lv, Player& p) {
  if (riders[0] != p.id) return;
  riders[0] = 0; p.riding = 0;
  World& w = lv.world(realm);
  const Registry& R = reg();
  for (float r : {1.1f, 1.8f}) for (int k = 0; k < 8; k++) {
    float a = yaw + PI / 2 + k * PI / 4, x = pos.x + std::cos(a) * r, z = pos.z - std::sin(a) * r;
    for (int y = ifloor(pos.y) + 1; y >= ifloor(pos.y) - 1; y--)
      if (R.tSolid[w.getBlock(ifloor(x), y - 1, ifloor(z))] && boxFree(w, vec3(x - 0.3f, y, z - 0.3f), vec3(x + 0.3f, y + 1.8f, z + 0.3f))) { p.pos = vec3(x, y, z); p.fallDist = 0; return; }
  }
  p.pos.y = pos.y + 1; p.fallDist = 0;
}
vec3 Minecart::riderPos(int) const {
  if (kind == "furnace") { float fx = -std::sin(yaw), fz = -std::cos(yaw); return vec3(pos.x - fx * 0.68f, pos.y + 0.3f, pos.z - fz * 0.68f); }
  return pos + vec3(0, 0.3f, 0);
}
static bool isCoal(const std::string& k) { return k == "coal" || k == "charcoal" || k == "coal_block"; }
static void consume(Player& p, ItemStack* held) { if (held && !p.creative()) { held->n--; if (held->n <= 0) held->clear(); } }

// Right click (minecart.js use + trains.js chains, the furnace engine)
bool Minecart::use(Level& lv, Player& p, ItemStack* held) {
  vec3 c = pos;
  if (held && held->id == "chain") {
    auto* vs = vehicleSystem(lv);
    auto& st = vs->ps(p.id);
    Minecart* a = dynamic_cast<Minecart*>(lv.entity(st.linkFrom));
    if (!a || a->removed || a == this) { st.linkFrom = id; vehToast(lv, p, "Now use the chain on the cart to join it to"); lv.fx->sound("chain_link", c, 12); return true; }
    st.linkFrom = 0;
    a->ensureUid(); ensureUid();
    if (std::hypot(a->pos.x - pos.x, a->pos.z - pos.z) > 3.5f || std::fabs(a->pos.y - pos.y) > 1.5f) { vehToast(lv, p, "Those carts are too far apart"); return true; }
    auto has = [](const std::vector<std::string>& v, const std::string& u) { return std::find(v.begin(), v.end(), u) != v.end(); };
    if (has(a->links, uid)) {
      a->links.erase(std::find(a->links.begin(), a->links.end(), uid));
      if (has(links, a->uid)) links.erase(std::find(links.begin(), links.end(), a->uid));
      p.give(ItemStack("chain", 1)); vehToast(lv, p, "Carts parted"); lv.fx->sound("chain_link", c, 12);
      return true;
    }
    if (a->links.size() >= 2 || links.size() >= 2) { vehToast(lv, p, "A cart can only be joined at its two ends"); return true; }
    a->links.push_back(uid); links.push_back(a->uid);
    consume(p, held);
    size_t n = trainOf(lv, this).size();
    vehToast(lv, p, n > 2 ? std::to_string(n) + " carts joined" : "Carts joined", "good");
    lv.fx->sound("chain_link", c, 12);
    return true;
  }
  if (kind == "minecart" && pet) { Entity* m = lv.entity(pet); std::string n = m ? m->type : "pet"; std::replace(n.begin(), n.end(), '_', ' '); vehToast(lv, p, "There's a " + n + " in this cart (sneak + use to lift it out)"); return true; }
  if (kind == "furnace" && held && isCoal(held->id)) {
    bool block = held->id == "coal_block";
    consume(p, held);
    fuel = block ? std::min(fuel + 1620.f, 3600.f) : std::min(fuel + 180.f, 1800.f);
    float dx = pos.x - p.pos.x, dz = pos.z - p.pos.z, d = std::hypot(dx, dz); if (d < 1e-4f) d = 1;
    pushX = dx / d; pushZ = dz / d;
    lv.fx->sound("ignite", c);
    return true;
  }
  if (kind == "furnace" && !p.riding) { if (!riders[0]) mount(lv, p); else vehToast(lv, p, "Someone is driving it"); return true; }
  if (kind == "minecart") { if (!p.riding) mount(lv, p); return true; }
  if (items()) {
    lv.fx->soundUi(p.id, "chest");
    if (lv.events) lv.events->emit("screenOpen", json::array({p.name, "container", json{{"entity", id}, {"name", T().key == std::string("chest") ? "Minecart with Chest" : "Minecart with Hopper"}}}));
    return true;
  }
  if (kind == "furnace") { vehToast(lv, p, "Feed it coal and it pushes itself along"); return true; }
  if (kind == "tnt" && held && held->def() && held->def()->str("igniter") == "flint") {
    prime(lv);
    if (!p.creative()) { held->d++; const ItemDef* d = held->def(); if (d->hasTool && d->tool.durability > 0 && held->d >= d->tool.durability) held->clear(); }
    return true;
  }
  return false;
}
void Minecart::hurt(Level& lv, Player* by, bool blast) {
  if (hitT > 0 && !blast) return;
  hitT = 0.25f; health -= blast ? 3 : 1; shakeT = 0.3f;
  lv.fx->sound("hit", pos);
  if (health <= 0 || (by && by->creative())) breakApart(lv, by ? by->creative() : lv.info.mode == "creative");
}
void Minecart::breakApart(Level& lv, bool creative) {
  if (removed) return;
  // its joins and its passenger go too (trains.js)
  for (auto& u : links) {
    for (auto& e : lv.entities()) if (auto* o = dynamic_cast<Minecart*>(e.get())) if (o->uid == u) o->links.erase(std::remove(o->links.begin(), o->links.end(), uid), o->links.end());
    if (!creative) dropStack(lv, realm, ItemStack("chain", 1), pos + vec3(0, 0.5f, 0), vec3(0, 2, 0));
  }
  links.clear();
  if (pet) { if (Entity* m = lv.entity(pet)) { m->riding = 0; m->data.erase("mountedOn"); m->pos.y += 0.6f; } pet = 0; }
  if (!cargo.is_null()) {
    dropStack(lv, realm, ItemStack(cargo.value("key", "stone"), 1), pos + vec3(0, 0.5f, 0), vec3(0, 2, 0));
    if (cargo["ent"].is_object() && cargo["ent"]["items"].is_array()) for (auto& it : cargo["ent"]["items"]) { ItemStack s = ItemStack::fromJson(it); if (!s.empty()) dropStack(lv, realm, s, pos + vec3(0, 0.5f, 0), vec3((rnd() - 0.5f) * 2, 2, (rnd() - 0.5f) * 2)); }
    cargo = nullptr;
  }
  if (!cargoBlock.empty() && !creative && fuse <= 0) dropStack(lv, realm, ItemStack(cargoBlock, 1), pos + vec3(0, 0.5f, 0), vec3(0, 2, 0));
  for (u32 r : riders) if (r) if (auto* p = dynamic_cast<Player*>(lv.entity(r))) dismount(lv, *p);
  removed = true;
  if (fuse > 0) { explode(lv, realm, pos + vec3(0, 0.5f, 0), 4); return; }
  if (json* it = items()) for (auto& j : *it) { ItemStack s = ItemStack::fromJson(j); if (!s.empty()) dropStack(lv, realm, s, pos + vec3(0, 0.5f, 0), vec3((rnd() - 0.5f) * 2, 2, (rnd() - 0.5f) * 2)); }
  if (!creative) dropStack(lv, realm, ItemStack(cargoBlock.empty() ? T().item : "minecart", 1), pos + vec3(0, 0.5f, 0), vec3(0, 2, 0));
}
bool Minecart::pick(vec3 eye, vec3 dir, float& t) const {
  // the body in its own frame (turned with the cart)
  vec2 o = toLocal(eye.x, eye.z);
  float c = std::cos(yaw), n = std::sin(yaw);
  vec3 ld(dir.x * c - dir.z * n, dir.y, dir.x * n + dir.z * c), lo(o.x, eye.y - pos.y, o.y);
  float L = half() - 0.02f;
  vec3 bmin(-0.48f, 0, -L), bmax(0.48f, 0.72f, L);
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
bool Minecart::loadCargo(Level& lv, const json& c) {
  if (removed || kind != "minecart" || !cargo.is_null() || pet || riders[0]) return false;
  if (c.value("kind", "") == "mob") {
    Entity* m = lv.entity(c.value("id", 0u));
    if (!m) return false;
    ensureUid(); pet = m->id; m->data["mountedOn"] = uid; m->riding = id;
    return true;
  }
  std::string key = c.value("key", "");
  static const std::unordered_map<std::string, std::string> KIND = {{"chest", "chest"}, {"barrel", "chest"}, {"trapped_chest", "chest"}, {"hopper", "hopper"}, {"furnace", "furnace"}, {"lit_furnace", "furnace"}, {"tnt", "tnt"}};
  auto it = KIND.find(key);
  if (it != KIND.end()) {
    become(it->second, key == "lit_furnace" ? "furnace" : key);
    if (json* items = this->items()) if (c.contains("ent") && c["ent"].is_object() && c["ent"]["items"].is_array()) {
      const json& src = c["ent"]["items"];
      for (size_t i = 0; i < src.size(); i++) { if (src[i].is_null()) continue; if (i < items->size()) (*items)[i] = src[i]; else dropStack(lv, realm, ItemStack::fromJson(src[i]), pos + vec3(0, 0.8f, 0)); }
    }
  } else cargo = json{{"key", key}, {"meta", c.value("meta", 0)}, {"ent", c.contains("ent") ? c["ent"] : json(nullptr)}};
  lv.fx->sound("chest", pos);
  return true;
}
json Minecart::unloadCargo(Level& lv) {
  if (pet) { u32 m = pet; pet = 0; if (Entity* e = lv.entity(m)) { e->riding = 0; e->data.erase("mountedOn"); } return json{{"kind", "mob"}, {"id", m}}; }
  if (!cargo.is_null()) { json c = cargo; cargo = nullptr; c["kind"] = "block"; return c; }
  if (kind != "minecart" && fuse < 0 && !riders[0]) {
    std::string key = !cargoBlock.empty() ? cargoBlock : T().block;
    json ent = nullptr;
    if (json* it = items()) ent = json{{"type", "chest"}, {"items", *it}};
    become("minecart");
    return json{{"kind", "block"}, {"key", key}, {"meta", 0}, {"ent", ent}};
  }
  return nullptr;
}

// ---------------------------------------------------------------------------------------- moving
void Minecart::tick(Level& lv, double dtd) {
  if (lv.isRemote()) return;
  float dt = (float)dtd;
  tickCommon(lv, dtd);
  if (removed) return;
  if (fuse > 0) {
    fuse -= dt;
    if (rnd() < dt * 20) lv.fx->particles("smoke", pos + vec3(0, 1.1f, 0), 1);
    if (fuse <= 0) {
      removed = true;
      for (u32 r : riders) if (r) if (auto* p = dynamic_cast<Player*>(lv.entity(r))) dismount(lv, *p);
      explode(lv, realm, pos + vec3(0, 0.5f, 0), 4);
      return;
    }
  }
  Player* seated = riders[0] ? dynamic_cast<Player*>(lv.entity(riders[0])) : nullptr;
  if (kind != "furnace" || !seated) { baseTick(lv, dtd, false); return; }
  // trains.js: driving from the seat behind the furnace; it pulls whatever is joined to it
  std::vector<Minecart*> carts = trainOf(lv, this);
  float fx = -std::sin(yaw), fz = -std::cos(yaw);
  float along = vel.x * fx + vel.z * fz;
  int drive = inp.mz < -0.3f ? 1 : inp.mz > 0.3f ? -1 : 0;
  if (drive && fuel <= 0 && !seated->creative()) {
    auto& st = vehicleSystem(lv)->ps(seated->id);
    if (vehicleSystem(lv)->time - st.noFuelT > 4) { st.noFuelT = (float)vehicleSystem(lv)->time; vehToast(lv, *seated, "The furnace needs coal (use coal while you sit here)", "warn"); }
    drive = 0;
  }
  if (drive) {
    float want = drive > 0 ? 7.f : -4.f, n = (float)carts.size(), pull = (8 + 2 * n) / n * 2;
    if (drive < 0 && along > 0.2f) { for (auto* c : carts) { float k = std::max(0.f, 1 - dt * 3); c->vel.x *= k; c->vel.z *= k; } }
    else if ((drive > 0 && along < want) || (drive < 0 && along > want)) { vel.x += fx * drive * pull * dt; vel.z += fz * drive * pull * dt; }
    if (!seated->creative()) fuel = std::max(0.f, fuel - dt);
    if (rnd() < dt * 6) lv.fx->particles("smoke", pos + vec3(fx * 0.68f, 1.05f, fz * 0.68f), 1, json{{"big", true}});
  }
  float savedFuel = fuel; fuel = 0;
  baseTick(lv, dtd, true);
  fuel = savedFuel;
}
void Minecart::baseTick(Level& lv, double dtd, bool driven) {
  float dt = (float)dtd;
  World& w = lv.world(realm);
  Player* rider = riders[0] ? dynamic_cast<Player*>(lv.entity(riders[0])) : nullptr;
  // the rider can give a push
  if (rider && !driven && inp.mz < -0.3f) {
    float lx = -std::sin(rider->yaw), lz = -std::cos(rider->yaw);
    if (std::hypot(vel.x, vel.z) < 4) { vel.x += lx * 5 * dt; vel.z += lz * 5 * dt; }
  }
  // people walking into an empty cart shove it; carts bump each other
  for (auto& e : lv.entities()) {
    if (e->removed || e->realm != realm || e.get() == this) continue;
    if (e->isPlayer()) {
      Player& p = static_cast<Player&>(*e);
      if (p.riding == id || p.dead || std::fabs(p.pos.y - pos.y) > 1.2f) continue;
      float dx = pos.x - p.pos.x, dz = pos.z - p.pos.z, d = std::hypot(dx, dz);
      if (d < 0.8f && d > 1e-3f) { float k = (0.8f - d) * 14 * dt; vel.x += dx / d * k * 4; vel.z += dz / d * k * 4; }
    } else if (auto* o = dynamic_cast<Minecart*>(e.get())) {
      if (std::fabs(o->pos.y - pos.y) > 1) continue;
      float dx = pos.x - o->pos.x, dz = pos.z - o->pos.z, d = std::hypot(dx, dz);
      if (d < 1.2f && d > 1e-3f) {
        // swap speeds along the line between them (an elastic knock)
        float nx = dx / d, nz = dz / d, va = vel.x * nx + vel.z * nz, vb = o->vel.x * nx + o->vel.z * nz;
        if (vb - va > 0) { float dv = vb - va; vel.x += nx * dv; vel.z += nz * dv; o->vel.x -= nx * dv; o->vel.z -= nz * dv; }
        pos.x += nx * (1.2f - d) * 0.5f; pos.z += nz * (1.2f - d) * 0.5f;
      }
    }
  }
  // furnace carts push themselves
  if (fuel > 0) {
    fuel -= dt;
    float along = vel.x * pushX + vel.z * pushZ;
    if (along < 4) { vel.x += pushX * 6 * dt; vel.z += pushZ * 6 * dt; }
    if (rnd() < dt * 6) lv.fx->particles("smoke", pos + vec3(0, 1, 0), 1, json{{"big", true}});
  }
  // move in small steps so fast carts don't skip rails
  float sp = glm::length(vel);
  int n = std::max(1, (int)std::ceil(sp * dt / 0.3f));
  for (int i = 0; i < n; i++) step(lv, w, dt / n);
  // hopper carts pick things up
  if (kind == "hopper" && !locked && (hopT += dt) > 0.2f) { hopT = 0; hopperPull(lv); }
  // carry the rider
  if (rider) { rider->pos = riderPos(0); rider->vel = vec3(vel.x, 0, vel.z); rider->fallDist = 0; rider->onGround = true; }
  seatPet(lv, pos + vec3(0, 0.3f, 0));
  // the rails sing as you go
  float v = std::hypot(vel.x, vel.z);
  if (onRail && v > 1) { rollT += dt * v; if (rollT > 3) { rollT = 0; lv.fx->sound("rail", pos, 20); } }
  if (pos.y < -40) removed = true;
}
void Minecart::turnTo(float target) {
  float d = std::fmod(target - yaw + PI * 3, TAU); if (d < 0) d += TAU; d -= PI;
  if (d > PI / 2) d -= PI; else if (d < -PI / 2) d += PI;
  yaw += d * 0.6f;
}
void Minecart::step(Level& lv, World& w, float dt) {
  const Registry& R = reg();
  int x0 = ifloor(pos.x), z0 = ifloor(pos.z), y0 = ifloor(pos.y + 0.02f);
  int by = 0; BlockId id = AIR;
  for (int yy : {y0, y0 - 1}) { BlockId b = w.getBlock(x0, yy, z0); if (rs::T().rail[b]) { id = b; by = yy; break; } }
  onRail = id != AIR;
  if (!onRail) {
    // off the rails: an ordinary heavy thing
    vel.y = std::max(-30.f, vel.y - 24 * dt);
    moveVehicle(lv, *this, dt);
    float f = onGround ? std::max(0.f, 1 - dt * 3) : std::max(0.f, 1 - dt * 0.2f);
    vel.x *= f; vel.z *= f;
    if (std::fabs(vel.x) + std::fabs(vel.z) > 0.1f) turnTo(std::atan2(-vel.x, -vel.z));
    pitch *= std::max(0.f, 1 - dt * 6);
    return;
  }
  int bx = x0, bz = z0, m = w.getMeta(bx, by, bz);
  u8 rk_ = rs::T().rail[id];
  int sh = railShape(id, m);
  const int* e0 = RAIL_EXITS[sh][0]; const int* e1 = RAIL_EXITS[sh][1];
  float p0x = bx + 0.5f + e0[0] * 0.5f, p0z = bz + 0.5f + e0[2] * 0.5f, p1x = bx + 0.5f + e1[0] * 0.5f, p1z = bz + 0.5f + e1[2] * 0.5f;
  float dx = p1x - p0x, dz = p1z - p0z, L2 = dx * dx + dz * dz, len = std::sqrt(L2), ux = dx / len, uz = dz / len;
  float v = vel.x * ux + vel.z * uz;
  // onto a new rail: keep full speed (projecting onto a curve's diagonal used to halve it every corner)
  u64 key = posKey(bx, by, bz);
  if (!rkSet || rk != key) {
    float spd = std::hypot(vel.x, vel.z);
    if (rkSet && spd > 0.05f) v = (((pos.x - p0x) * dx + (pos.z - p0z) * dz) / L2 < 0.5f ? 1 : -1) * spd;
    rk = key; rkSet = true;
  }
  vel.y = 0;
  // down the slope
  if (e0[1] != e1[1]) v += (e1[1] > e0[1] ? -1 : 1) * 5 * dt;
  // powered rails speed you up (or stop you dead when they're off)
  if (rk_ == RAIL_POWERED) {
    if (m & 8) {
      if (std::fabs(v) > 0.05f) v += (v > 0 ? 1 : -1) * 22 * dt;
      else {
        // standing start: push away from a wall at one end
        bool s0 = R.tSolid[w.getBlock(bx + e0[0], by + e0[1], bz + e0[2])], s1 = R.tSolid[w.getBlock(bx + e1[0], by + e1[1], bz + e1[2])];
        if (s0 && !s1) v = 2; else if (s1 && !s0) v = -2;
      }
    } else { v *= std::max(0.f, 1 - dt * 14); if (std::fabs(v) < 0.2f) v = 0; }
  }
  if (rk_ == RAIL_ACTIVATOR) activate(lv, (m & 8) != 0);
  // rolling resistance
  v *= std::pow(riders[0] || fuel > 0 ? 0.997f : 0.985f, dt * 20);
  v = clampv(v, -CART_MAX, CART_MAX);
  // onto the line, then along it
  float tt = clampv(((pos.x - p0x) * dx + (pos.z - p0z) * dz) / L2, 0.f, 1.f);
  float nt = tt + v * dt / len;
  if (nt > 1 || nt < 0) {
    // is there track (or open air) past this end?
    const int* ex = nt > 1 ? e1 : e0;
    int cx = bx + ex[0], cz = bz + ex[2], cy = by + ex[1];
    BlockId cb = w.getBlock(cx, cy, cz);
    bool more = rs::T().rail[cb] || rs::T().rail[w.getBlock(cx, cy - 1, cz)];
    if (!more && R.tSolid[cb] && !R.tShape[cb]) {
      // a wall: bump to a stop (and turn a furnace cart round)
      nt = nt > 1 ? 1 - 0.002f / len : 0.002f / len;
      if (std::fabs(v) > 3) lv.fx->sound("thud", pos, 16);
      v = 0; pushX = -pushX; pushZ = -pushZ;
    }
  }
  float ct = clampv(nt, 0.f, 1.f);
  pos.x = p0x + dx * nt; pos.z = p0z + dz * nt;
  pos.y = by + RAIL_Y + lerpf((float)e0[1], (float)e1[1], ct);
  vel.x = ux * v; vel.z = uz * v;
  // face along the track, without spinning round when reversing
  float along = std::atan2(-ux, -uz);
  turnTo(along);
  float rise = (e1[1] - e0[1]) / len;
  float wantPitch = std::atan(rise) * (std::cos(yaw - along) > 0 ? 1 : -1);
  pitch += (wantPitch - pitch) * std::min(1.f, dt * 12);
}
void Minecart::activate(Level& lv, bool on) {
  if (kind == "hopper") locked = on;
  if (!on) return;
  if (riders[0]) if (auto* p = dynamic_cast<Player*>(lv.entity(riders[0]))) dismount(lv, *p);
  if (kind == "tnt") prime(lv);
}
void Minecart::hopperPull(Level& lv) {
  Redstone* rsys = redstoneOf(lv, realm);
  json* it = items();
  if (!rsys || !it) return;
  int x = ifloor(pos.x), y = ifloor(pos.y + 0.02f), z = ifloor(pos.z);
  Container me; me.kind = Container::Box; me.items = it; me.cart = this; me.x = x; me.y = y; me.z = z;
  Container src = rsys->inv(x, y + 1, z);
  if (src && !src.cart) { if (rsys->moveOne(src, 0, me, 1)) rsys->changedInv(src); return; }
  for (auto& e : lv.entities()) {
    if (e->removed || e->type != "item" || e->realm != realm) continue;
    if (std::fabs(e->pos.x - pos.x) > 1 || std::fabs(e->pos.z - pos.z) > 1 || e->pos.y < pos.y - 0.2f || e->pos.y > pos.y + 1.5f) continue;
    ItemStack s = ItemStack::fromJson(e->data["stack"]);
    if (s.empty()) continue;
    if (rsys->insertOne(me, 1, s)) { s.n--; if (s.n <= 0) e->removed = true; else e->data["stack"] = s.toJson(); return; }
  }
}

// ---------------------------------------------------------------------------------------- saving
json Minecart::save() const {
  json j = Entity::save();
  json& d = j["data"];
  json common = saveCommon();
  for (auto& [k, v] : common.items()) d[k] = v;
  d["kind"] = kind;
  if (fuel > 0) { d["fuel"] = std::round(fuel); d["px"] = pushX; d["pz"] = pushZ; } else { d.erase("fuel"); d.erase("px"); d.erase("pz"); }
  if (!links.empty()) d["lk"] = links; else d.erase("lk");
  if (!cargo.is_null()) d["cg"] = cargo; else d.erase("cg");
  if (!cargoBlock.empty()) d["cb"] = cargoBlock; else d.erase("cb");
  if (fuse > 0) d["fuse"] = fuse; else d.erase("fuse");
  return j;
}
void Minecart::load(const json& j) {
  Entity::load(j);
  type = "minecart";
  loadCommon(data);
  kind = cartType(data.value("kind", "minecart")).key;
  fuel = data.value("fuel", 0.f); pushX = data.value("px", 0.f); pushZ = data.value("pz", 0.f); fuse = data.value("fuse", -1.f);
  if (data["lk"].is_array()) for (auto& u : data["lk"]) if (u.is_string()) links.push_back(u);
  if (data.contains("cg")) cargo = data["cg"];
  cargoBlock = data.value("cb", "");
  if (T().slots) {
    json& it = data["items"];
    if (!it.is_array()) it = json::array();
    while ((int)it.size() < T().slots) it.push_back(nullptr);
  }
}

// ---------------------------------------------------------------------------------------- the world's side
Minecart* cartAt(Level& lv, int realm, int x, int y, int z, bool withItems) {
  for (auto& e : lv.entities()) {
    auto* c = dynamic_cast<Minecart*>(e.get());
    if (!c || c->removed || c->realm != realm || (withItems && !c->items())) continue;
    int cy = ifloor(c->pos.y + 0.02f);
    if (ifloor(c->pos.x) == x && ifloor(c->pos.z) == z && (cy == y || cy == y + 1)) return c;
  }
  return nullptr;
}
Minecart* placeCart(Level& lv, int realm, const std::string& kind, int x, int y, int z, int) {
  World& w = lv.world(realm);
  BlockId id = w.getBlock(x, y, z);
  if (!rs::T().rail[id]) return nullptr;
  const int* e = RAIL_EXITS[railShape(id, w.getMeta(x, y, z))][0];
  auto c = std::make_unique<Minecart>();
  c->realm = realm;
  c->become(kind);
  c->pos = vec3(x + 0.5f, y + RAIL_Y, z + 0.5f);
  c->yaw = std::atan2((float)-e[0], (float)-e[2]);
  c->health = 3;
  c->ensureUid();
  lv.fx->sound("rail", vec3(x, y, z) + vec3(0.5f), 16);
  return static_cast<Minecart*>(lv.spawn(std::move(c)));
}
bool placeCartItem(Level& lv, Player& p, ItemStack& held, ivec3 hit) {
  World& w = lv.world(p.realm);
  const ItemDef* def = held.def();
  if (!def || !rs::T().rail[w.getBlock(hit.x, hit.y, hit.z)]) { vehToast(lv, p, "Set a minecart on a rail"); return false; }
  if (cartAt(lv, p.realm, hit.x, hit.y, hit.z)) return false;
  Minecart* c = placeCart(lv, p.realm, def->str("cart"), hit.x, hit.y, hit.z, 0);
  if (!c) return false;
  c->yaw = p.yaw;
  if (!p.creative()) { held.n--; if (held.n <= 0) held.clear(); }
  return true;
}

// ---- trains (trains.js) ----
static std::unordered_map<std::string, Minecart*> cartsByUid(Level& lv) {
  std::unordered_map<std::string, Minecart*> m;
  for (auto& e : lv.entities()) if (auto* c = dynamic_cast<Minecart*>(e.get())) if (!c->removed && !c->uid.empty()) m[c->uid] = c;
  return m;
}
std::vector<Minecart*> trainOf(Level& lv, Minecart* c) {
  std::vector<Minecart*> out{c};
  if (c->links.empty()) return out;
  auto byUid = cartsByUid(lv);
  std::unordered_set<Minecart*> seen{c};
  for (size_t i = 0; i < out.size(); i++) for (auto& u : out[i]->links) {
    auto it = byUid.find(u);
    if (it != byUid.end() && !seen.count(it->second)) { seen.insert(it->second); out.push_back(it->second); }
  }
  return out;
}
static void unlink(Minecart* a, Minecart* b) {
  a->links.erase(std::remove(a->links.begin(), a->links.end(), b->uid), a->links.end());
  b->links.erase(std::remove(b->links.begin(), b->links.end(), a->uid), b->links.end());
}
static float linkLen(const Minecart* a, const Minecart* b) { return a->half() + b->half() + 0.06f; }
void trainStep(Level& lv, double dtd) {
  float dt = (float)dtd;
  auto byUid = cartsByUid(lv);
  std::unordered_set<std::string> done;
  bool creative = lv.info.mode == "creative";
  for (auto& [u, c] : byUid) {
    if (c->links.empty()) continue;
    for (auto lu : std::vector<std::string>(c->links)) {
      auto it = byUid.find(lu);
      if (it == byUid.end()) {   // its partner is gone (or not loaded yet: keep the join while it might come back)
        bool exists = false;
        for (auto& e : lv.entities()) if (auto* o = dynamic_cast<Minecart*>(e.get())) if (o->uid == lu && !o->removed) exists = true;
        if (!exists && c->age > 2) c->links.erase(std::remove(c->links.begin(), c->links.end(), lu), c->links.end());
        continue;
      }
      Minecart* o = it->second;
      std::string key = c->uid < o->uid ? c->uid + ":" + o->uid : o->uid + ":" + c->uid;
      if (!done.insert(key).second) continue;
      float dx = o->pos.x - c->pos.x, dz = o->pos.z - c->pos.z, dy = o->pos.y - c->pos.y, d = std::hypot(dx, dz);
      // pulled too far apart (a broken track, a fall): the chain snaps
      if (d > 6 || std::fabs(dy) > 3 || o->realm != c->realm) {
        unlink(c, o);
        if (!creative) dropStack(lv, c->realm, ItemStack("chain", 1), (c->pos + o->pos) * 0.5f + vec3(0, 0.5f, 0));
        lv.fx->sound("chain_link", c->pos, 16);
        continue;
      }
      if (d < 1e-3f) continue;
      float nx = dx / d, nz = dz / d, mc = c->kind == "furnace" ? 2.f : 1.f, mo = o->kind == "furnace" ? 2.f : 1.f, tot = mc + mo;
      float err = d - linkLen(c, o);
      if (std::fabs(err) > 0.01f) {
        // hold the distance: each moves along the line between them (the rails put them back on the track)
        float k = std::min(1.f, dt * 30);
        c->pos.x += nx * err * (mo / tot) * k; c->pos.z += nz * err * (mo / tot) * k;
        o->pos.x -= nx * err * (mc / tot) * k; o->pos.z -= nz * err * (mc / tot) * k;
      }
      // and share their speed along it, the heavier engine counting for more
      float vc = c->vel.x * nx + c->vel.z * nz, vo = o->vel.x * nx + o->vel.z * nz, avg = (vc * mc + vo * mo) / tot;
      c->vel.x += nx * (avg - vc); c->vel.z += nz * (avg - vc); o->vel.x += nx * (avg - vo); o->vel.z += nz * (avg - vo);
    }
  }
}

// ---------------------------------------------------------------------------------------- laying track
// Neighbouring rail in direction h: its height difference (0, 1 up, -1 down), or 99 for none.
static int railNeighbour(const World& w, int x, int y, int z, int h) {
  int nx = x + DIRX[h], nz = z + DIRZ[h];
  if (rs::T().rail[w.getBlock(nx, y, nz)]) return 0;
  if (rs::T().rail[w.getBlock(nx, y + 1, nz)]) return 1;
  if (rs::T().rail[w.getBlock(nx, y - 1, nz)]) return -1;
  return 99;
}
// The exit of the rail at (x,y,z) toward direction h, if it has one.
static const int* railExitTo(const World& w, int x, int y, int z, int h) {
  BlockId id = w.getBlock(x, y, z);
  if (!rs::T().rail[id]) return nullptr;
  for (auto& e : RAIL_EXITS[railShape(id, w.getMeta(x, y, z))]) if (e[0] == DIRX[h] && e[2] == DIRZ[h]) return e;
  return nullptr;
}
// Is the rail at (x,y,z) joined to the track in direction h (both ends meeting at the right height)?
static bool railJoined(const World& w, int x, int y, int z, int h) {
  const int* e = railExitTo(w, x, y, z, h);
  if (!e) return false;
  int nx = x + DIRX[h], nz = z + DIRZ[h], back = (h + 2) & 3;
  if (e[1]) { const int* b = railExitTo(w, nx, y + 1, nz, back); return b && !b[1]; }
  const int* same = railExitTo(w, nx, y, nz, back);
  if (same && !same[1]) return true;
  const int* low = railExitTo(w, nx, y - 1, nz, back);
  return low && low[1] == 1;
}
int railShapeFor(const World& w, int x, int y, int z, BlockId id, int prefer) {
  bool normal = rs::T().rail[id] == RAIL_NORMAL;
  // a neighbour counts if it's free to join us: it already points our way, or it has an end to spare
  int has[4];
  for (int h = 0; h < 4; h++) {
    has[h] = 99;
    int dy = railNeighbour(w, x, y, z, h);
    if (dy == 99) continue;
    int nx = x + DIRX[h], nz = z + DIRZ[h], ny = y + dy, back = (h + 2) & 3;
    if (railExitTo(w, nx, ny, nz, back)) { has[h] = dy; continue; }
    int joins = 0;
    for (int k = 0; k < 4; k++) if (k != back && railJoined(w, nx, ny, nz, k)) joins++;
    if (joins < 2) has[h] = dy;
  }
  bool N = has[0] != 99, E = has[1] != 99, S = has[2] != 99, Wd = has[3] != 99;
  if (normal) {
    if (S && E && !N && !Wd) return 6;
    if (S && Wd && !N && !E) return 7;
    if (N && Wd && !S && !E) return 8;
    if (N && E && !S && !Wd) return 9;
  }
  bool ns = N || S, ew = E || Wd;
  if ((ns && !ew) || (!ns && !ew && (prefer & 1) == 0) || (ns && ew && (prefer & 1) == 0)) {
    if (has[0] == 1) return 4;
    if (has[2] == 1) return 5;
    return 0;
  }
  if (has[1] == 1) return 2;
  if (has[3] == 1) return 3;
  return 1;
}
// After a rail is laid, loose ends next to it turn to meet it.
void railRefreshAround(World& w, int x, int y, int z) {
  for (int h = 0; h < 4; h++) for (int dy : {0, 1, -1}) {
    int nx = x + DIRX[h], ny = y + dy, nz = z + DIRZ[h];
    BlockId id = w.getBlock(nx, ny, nz);
    if (!rs::T().rail[id]) continue;
    int joins = 0;
    for (int k = 0; k < 4; k++) if (railJoined(w, nx, ny, nz, k)) joins++;
    if (joins >= 2) continue;
    int m = w.getMeta(nx, ny, nz), cur = railShape(id, m);
    const int* ex = RAIL_EXITS[cur][0];
    int sh = railShapeFor(w, nx, ny, nz, id, ex[0] ? 1 : 0);
    if (sh != cur) w.setBlock(nx, ny, nz, id, rs::T().rail[id] == RAIL_NORMAL ? sh : (m & 8) | sh);
  }
}

} // namespace dq
