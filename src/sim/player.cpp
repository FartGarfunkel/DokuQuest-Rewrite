// sim/player.cpp — port of game/player.js (Player.move, climbStep, health, XP, save) and
// game/movement.js (ledge shimmy, coyote time, jump buffering).
#include "sim/player.h"
#include "sim/inventory.h"
#include "sim/blockinfo.h"
#include "world/shapes.h"

namespace dq {

Player::Player() {
  type = "player"; w = PLAYER_W; h = PLAYER_H; hp = maxHp = 20;
  inv.resize(INV_MAIN); armor.resize(INV_ARMOR); offhand.resize(INV_OFFHAND);
  back.resize(1); hook.resize(1); charm.resize(1); cape.resize(1); ender.resize(INV_ENDER);
  stats = {{"blocksMined", 0}, {"mobsKilled", 0}, {"bossesKilled", 0}, {"deaths", 0}};
}

float Player::heal(float n) { float before = hp; hp = std::min(maxHp, hp + n); return hp - before; }
void Player::recalcMax() {
  float m = 20 + 2 * std::min(10, heartCrystals) + 2 * charmBonus(*this, "hearts");
  if (m != maxHp) { maxHp = m; if (hp > m) hp = m; }
}
int Player::addXP(int n) {
  xp += n;
  int ups = 0;
  while (xp >= xpForLevel(level)) { xp -= xpForLevel(level); level++; ups++; }
  return ups;
}

// ---- ledges -----------------------------------------------------------------------------------
// The top of the ledge in cell (x, cy, z): the top of that block's own collision boxes.
static float blockTopAt(const World& w, int x, int y, int z, BlockId id) {
  if (!hasShapeBoxes(id)) return 1;
  static thread_local std::vector<PixBox> bs;
  if (!worldBoxes(w, x, y, z, id, SHAPE_COLLIDE, bs)) return 1;
  float m = 0; for (auto& b : bs) m = std::max(m, b.y1 / 16.f);
  return m;
}
bool ledgeSurface(const World& w, int x, int cy, int z, float& surf) {
  BlockId id = w.getBlockPhys(x, cy, z);
  if (!reg().tSolid[id] || bhas(id, BF::Scaffold)) return false;
  float h = blockTopAt(w, x, cy, z, id);
  if (h < 0.75f) return false;
  float s = cy + h;
  if (h <= 1 + 1e-6f) {
    BlockId up = w.getBlockPhys(x, cy + 1, z);
    if (reg().tSolid[up] && !bhas(up, BF::Scaffold)) { float hu = blockTopAt(w, x, cy + 1, z, up); if (hu > 0 && hu <= 0.5f) s = cy + 1 + hu; }
  }
  surf = s;
  return true;
}
bool ledgeRoom(const World& w, int bx, int bz, float s, float halfW) {
  double r = halfW - 0.01;
  return !collides(w, bx + 0.5 - r, s + 0.02, bz + 0.5 - r, bx + 0.5 + r, s + PLAYER_H, bz + 0.5 + r);
}

// ---- movement -----------------------------------------------------------------------------------
MoveInfo Player::climbStep(const World& wd, float dt) {
  LedgeClimb& C = *climb;
  const float D = 0.55f;
  C.t += dt;
  float k = std::min(1.f, C.t / D), up = std::min(1.f, k / 0.6f), over = std::max(0.f, (k - 0.55f) / 0.45f);
  auto e = [](float v) { return v * v * (3 - 2 * v); };
  pos.y = C.from.y + (C.up - C.from.y) * e(up);
  pos.x = C.from.x + (C.to.x - C.from.x) * e(over);
  pos.z = C.from.z + (C.to.z - C.from.z) * e(over);
  vel = vec3(0); hasFallStart = false; onGround = false;
  h = PLAYER_H; eye = EYE;
  if (k >= 1) {
    float ty = C.to.y;
    climb.reset(); pos.y = ty; onGround = true;
    if (collides(wd, pos.x - w, pos.y, pos.z - w, pos.x + w, pos.y + PLAYER_H, pos.z + w)) { crawling = true; h = SWIM_H; eye = SWIM_EYE; }
  }
  return {0, k >= 1};
}

MoveInfo Player::baseMove(const World& wd, const PlayerInput& inp, float dt, const MoveOpts& opts) {
  const Registry& R = reg();
  sneaking = inp.sneak;
  const Contacts& c = contacts;
  bool inWater = c.water, inLava = c.lava;
  // ---- body actions: hang from a ledge, pull yourself up ----
  bool jumpEdge = inp.jump && !jumpHeld; jumpHeld = inp.jump;
  if (inp.crawl) crawlWant = !crawlWant;
  if (crawlWant && (inWater || flying)) crawlWant = false;
  if (climb) return climbStep(wd, dt);
  if (hang) {
    LedgeHang& H = *hang;
    H.t += dt;
    float S = 0; bool okS = ledgeSurface(wd, H.bx, H.cy, H.bz, S);
    if (okS && std::fabs(S - H.surf) <= 0.13f && ledgeRoom(wd, H.bx, H.bz, S, w)) H.surf = S;
    if (flying || (inWater && !H.water) || !okS || H.surf != S) hang.reset();
    else if (inp.sneak || inp.mz > 0.4f) { hang.reset(); vel.y = -1; fallStart = pos.y; hasFallStart = true; }   // let go
    else if (jumpEdge || (inp.jump && H.t > 0.3f) || (inp.mz < -0.4f && H.t > 0.6f)) {
      // pull up and over onto the ledge
      float tx = H.dx ? H.bx + 0.5f - H.dx * 0.15f : pos.x, tz = H.dz ? H.bz + 0.5f - H.dz * 0.15f : pos.z;
      climb = LedgeClimb{0, pos, vec3(tx, H.surf, tz), H.surf + 0.02f};
      hang.reset();
      return climbStep(wd, 0);
    } else {
      vel = vec3(0); pos.y = H.surf - 1.75f; pos.x = H.x; pos.z = H.z;
      hasFallStart = false; onGround = false; crawling = false; h = PLAYER_H; eye = EYE;
      bobAmt *= 0.9f;
      return {0, false};
    }
  }
  // modern swimming: sprint forward with your head under water and you swim like a fish
  if (inWater && !flying && !sneaking && inp.sprint && inp.mz < -0.3f && (c.eyeWater || swimming)) swimming = true;
  else if (swimming && (!inWater || inp.mz > -0.2f || flying)) {
    swimming = false;
    if (collides(wd, pos.x - w, pos.y, pos.z - w, pos.x + w, pos.y + PLAYER_H, pos.z + w)) crawling = true;   // no room to stand up
  }
  // crawl when you ask to, or sneak into a gap one block high
  if (crawlWant && onGround && !inWater && !flying) crawling = true;
  if ((sneaking || inp.crawlHeld) && onGround && !inWater && !flying && !crawling && (std::fabs(inp.mx) + std::fabs(inp.mz)) > 0.3f) {
    float s0 = std::sin(yaw), c0 = std::cos(yaw);
    float dx = c0 * inp.mx + s0 * inp.mz, dz = -s0 * inp.mx + c0 * inp.mz, dl = std::hypot(dx, dz); if (dl == 0) dl = 1;
    int ax = ifloor(pos.x + dx / dl * (w + 0.35f)), az = ifloor(pos.z + dz / dl * (w + 0.35f)), fy = ifloor(pos.y + 0.05f);
    if (!R.tSolid[wd.getBlockPhys(ax, fy, az)] && R.tSolid[wd.getBlockPhys(ax, fy + 1, az)]) { crawling = true; crawlHold = 0.5f; }
  }
  if (crawlHold > 0) crawlHold -= dt;
  if (crawling && !swimming && !crawlWant && !(crawlHold > 0) && !collides(wd, pos.x - w, pos.y, pos.z - w, pos.x + w, pos.y + PLAYER_H, pos.z + w)) crawling = false;
  h = swimming || crawling ? SWIM_H : PLAYER_H;
  float speed = flying ? (inp.sprint ? 18.f : 10.f) : sneaking ? SNEAK : (inp.sprint && inp.mz < -0.3f ? SPRINT : WALK);
  if (flying) sneaking = false;
  sprinting = speed == SPRINT && !swimming;
  if (crawling) speed = SNEAK * 1.1f;
  if (swimming) speed = SWIM;
  else if (inWater) speed *= 0.46f;
  if (inLava) speed *= 0.4f;
  if (c.web) speed *= 0.2f;
  if (slow > 0) speed *= 0.55f;
  speed *= opts.slow;
  BlockId under = wd.getBlock(ifloor(pos.x), ifloor(pos.y - 0.1f), ifloor(pos.z));
  if (onGround && !flying) speed *= binfo(under).slow;   // soul sand and honey drag at your feet
  // desired horizontal velocity in world space
  float s = std::sin(yaw), co = std::cos(yaw);
  float fx = -s, fz = -co, rx = co, rz = -s;
  float tx = (rx * inp.mx - fx * inp.mz) * speed, tz = (rz * inp.mx - fz * inp.mz) * speed;
  bool slippery = onGround && bhas(under, BF::Slippery);
  float accel = flying ? 8.f : onGround ? (slippery ? 2.f : 14.f) : (inWater || inLava ? 6.f : 3.f);
  float k = std::min(1.f, accel * dt);
  vel.x += (tx - vel.x) * k; vel.z += (tz - vel.z) * k;
  // vertical
  if (swimming) {
    vec3 L = lookVector(yaw, pitch);
    float want = inp.mz < -0.3f ? -inp.mz : 0;
    float kk = std::min(1.f, 5 * dt);
    vel.x += (L.x * SWIM * want - vel.x) * kk;
    vel.z += (L.z * SWIM * want - vel.z) * kk;
    float up = !c.eyeWater && L.y > 0 && !inp.jump ? std::min(L.y, 0.05f) : L.y;   // skim along the surface
    vel.y += (up * SWIM * want + (inp.jump ? 2.f : 0.f) - vel.y) * kk;
  } else if (inWater || inLava) {
    // the classic game's water: sink slowly, kick up with jump, dive with sneak, drag on every move
    float drag = std::pow(inLava ? 0.5f : 0.8f, 20 * dt);
    vel.y -= (inLava ? 8.f : 2.f) * dt;
    if (inp.jump) vel.y += 16 * dt;
    else if (sneaking) vel.y -= 16 * dt;
    vel.y *= drag;
    if (inp.jump && hitWallLast) vel.y = std::max(vel.y, 5.5f);   // hop out at the edge of a pool
  } else if (c.ladder) {
    vel.y -= GRAVITY * dt;
    vel.y = std::max(vel.y, -2.4f);
    if (inp.jump || (inp.mz < -0.3f && hitWallLast) || (inp.mz < -0.3f && !onGround)) vel.y = 2.6f;
    if (sneaking && vel.y < 0) vel.y = c.scaffold ? -2.2f : 0.f;
    hasFallStart = false;
  } else {
    vel.y -= GRAVITY * dt;
    if (vel.y < -60) vel.y = -60;
    if (c.web) vel.y = std::max(vel.y, -1.f);
    if (inp.jump && onGround) { vel.y = JUMP_V * (bhas(under, BF::Sticky) ? 0.55f : 1.f); if (sprinting) { vel.x *= 1.15f; vel.z *= 1.15f; } }
  }
  if (flying) { vel.y = (inp.jump ? 7.5f : 0.f) - (inp.sneak ? 7.5f : 0.f); hasFallStart = false; }
  // auto-jump (touch): walking into a one-block step
  if (opts.autoJump && onGround && !inWater && (std::fabs(tx) + std::fabs(tz)) > 0.5f && speed > 0) {
    float ahead = 0.45f, nx = pos.x + (tx / speed) * ahead, nz = pos.z + (tz / speed) * ahead;
    int fy = ifloor(pos.y + 0.05f);
    if (R.tSolid[wd.getBlockPhys(ifloor(nx), fy, ifloor(nz))] && !R.tSolid[wd.getBlockPhys(ifloor(nx), fy + 1, ifloor(nz))] && !R.tSolid[wd.getBlockPhys(ifloor(nx), fy + 2, ifloor(nz))]
        && !R.tSolid[wd.getBlockPhys(ifloor(pos.x), fy + 2, ifloor(pos.z))]) vel.y = JUMP_V;
  }
  float ox = pos.x, oz = pos.z, vyBefore = vel.y;
  bool wasGround = onGround;
  // a moment after stepping off a little lip you can still step over the next one
  airStep = !onGround && !flying && !inWater && !inLava && !c.ladder && vel.y <= 0.5f && airT < 0.25f ? 0.26f : 0.f;
  MoveOut mo = moveEntity(wd, *this, dt, {0.6f, airStep, sneaking});
  hitWall = mo.hitWall; hitCeiling = mo.hitCeiling;
  // slime blocks throw you back up (sneak to land softly)
  if (!wasGround && onGround && vyBefore < -2.5f && !sneaking) {
    BlockId ub = wd.getBlock(ifloor(pos.x), ifloor(pos.y - 0.1f), ifloor(pos.z));
    if (bhas(ub, BF::Bouncy)) { vel.y = -vyBefore * 0.82f; onGround = false; hasFallStart = false; bounced = true; }
  }
  if (flying && onGround && !inp.jump) flying = false;
  hitWallLast = hitWall;
  // jumped into a wall: catch the top edge if it's in reach (a ledge up to 2.7 above your feet)
  if ((!onGround || inWater) && !flying && !inLava && !c.ladder && !crawling && !swimming && hitWall && inp.mz < -0.3f && vel.y < 5 && !sneaking && !opts.noLedge) {
    vec3 L = lookVector(yaw, pitch);
    int dx = 0, dz = 0;
    if (std::fabs(L.x) > std::fabs(L.z)) dx = L.x > 0 ? 1 : L.x < 0 ? -1 : 0; else dz = L.z > 0 ? 1 : L.z < 0 ? -1 : 0;
    int bx = ifloor(pos.x + dx * (w + 0.2f)), bz = ifloor(pos.z + dz * (w + 0.2f));
    for (int cy = ifloor(pos.y); cy <= ifloor(pos.y + 2.7f); cy++) {
      float S;
      if (!ledgeSurface(wd, bx, cy, bz, S)) continue;
      float reach = S - pos.y;
      if (reach < 1.0f || reach > 2.7f) continue;
      if (!ledgeRoom(wd, bx, bz, S, w)) continue;
      float hy = S - 1.75f; double ww = w - 0.01;
      if (collides(wd, pos.x - ww, std::min(pos.y, hy) + 0.001, pos.z - ww, pos.x + ww, S + PLAYER_H, pos.z + ww)) continue;
      LedgeHang H; H.bx = bx; H.bz = bz; H.cy = cy; H.surf = S; H.dx = dx; H.dz = dz; H.x = pos.x; H.z = pos.z; H.water = inWater;
      hang = H;
      vel = vec3(0); pos.y = hy; hasFallStart = false; justGrabbed = true;
      break;
    }
  }
  // sneaking keeps you from walking off edges
  if (sneaking && wasGround && vel.y <= 0) {
    auto supported = [&](float x, float z) { return collides(wd, x - w + 0.05, pos.y - 0.6, z - w + 0.05, x + w - 0.05, pos.y - 0.01, z + w - 0.05); };
    // the half-reverted spot is checked against walls too (it was never swept): never end up inside a block
    auto clear = [&](float x, float z) { return !collides(wd, x - w, pos.y + 0.01, z - w, x + w, pos.y + h, z + w); };
    if (!supported(pos.x, pos.z)) {
      if (supported(ox, pos.z) && clear(ox, pos.z)) pos.x = ox;
      else if (supported(pos.x, oz) && clear(pos.x, oz)) pos.z = oz;
      else { pos.x = ox; pos.z = oz; }
      onGround = true; vel.y = 0;
    }
  }
  eye = swimming || crawling ? SWIM_EYE : sneaking ? EYE_SNEAK : EYE;
  if (onGround && !contacts.lava && R.tSolid[wd.getBlock(ifloor(pos.x), ifloor(pos.y - 0.1f), ifloor(pos.z))]) { lastSafe = pos; hasLastSafe = true; }
  // view bob and the stroke of arms and legs in the water
  float hs = std::hypot(vel.x, vel.z);
  bobAmt += ((onGround ? std::min(1.f, hs / WALK) : 0.f) - bobAmt) * std::min(1.f, dt * 8);
  bob += hs * dt * 1.6f;
  swimPh = std::fmod(swimPh + dt * (contacts.water ? 2.4f + hs * 0.5f : 1.2f), 628.3f);
  return {hs, !wasGround && onGround};
}

MoveInfo Player::move(const World& wd, const PlayerInput& inp, float dt, const MoveOpts& opts) {
  // ---- along the ledge, hand over hand (movement.js) ----
  if (hang && !flying && std::fabs(inp.mx) > 0.3f && !inp.jump && !inp.sneak && !(inp.mz > 0.4f) && !(inp.mz < -0.4f && hang->t > 0.6f)) {
    LedgeHang& H = *hang;
    float side = inp.mx > 0 ? 1.f : -1.f, rx = (float)-H.dz, rz = (float)H.dx, step = side * 1.7f * dt;
    float nx = H.x + rx * step, nz = H.z + rz * step, surf = H.surf;
    int lx = H.dx ? H.bx : ifloor(nx + rx * side * (w + 0.05f)), lz = H.dz ? H.bz : ifloor(nz + rz * side * (w + 0.05f));
    float S = 0; bool okS = ledgeSurface(wd, lx, H.cy, lz, S);
    float y = okS ? S - 1.75f : 0;
    bool ok = okS && std::fabs(S - surf) <= 0.13f && ledgeRoom(wd, lx, lz, S, w) && !collides(wd, nx - w, y, nz - w, nx + w, y + PLAYER_H, nz + w);
    if (ok) {
      H.x = nx; H.z = nz;
      if (H.dx) H.bz = ifloor(nz); else H.bx = ifloor(nx);
      if (H.bx == lx && H.bz == lz) H.surf = S;
      H.shimPh += std::fabs(step) * 3.4f;
      H.shimT = 0.15f;
      if ((H.scrapeT -= dt) <= 0) { H.scrapeT = 0.45f; shimSound = true; }   // a scrape of stone now and then
    }
  }
  if (hang && hang->shimT > 0) hang->shimT -= dt;
  // ---- a jump just too late (walked off the edge) or just too early (not landed yet) ----
  bool jumpEdge = inp.jump && !mvJumpHeld; mvJumpHeld = inp.jump;
  if (jumpEdge) jumpBuf = 0.14f;
  else if (jumpBuf > 0) jumpBuf -= dt;
  const Contacts& c = contacts;
  bool free = !flying && !c.water && !c.lava && !c.ladder && !hang && !climb && !swimming;
  if (free && !onGround && jumpEdge && airT < 0.13f && !jumped && vel.y <= 0.5f) { vel.y = JUMP_V; jumped = true; jumpBuf = 0; }
  bool wasGround = onGround;
  MoveInfo r = baseMove(wd, inp, dt, opts);
  if (onGround) {
    airT = 0; jumped = false;
    if (!wasGround && free && jumpBuf > 0 && !inp.jump && !sneaking) { vel.y = JUMP_V; onGround = false; jumped = true; jumpBuf = 0; }
  } else {
    airT += dt;
    if (vel.y > 1 && wasGround) jumped = true;
  }
  return r;
}

// ---- save record: the original's Player.toJSON keys (+ engine fields) ----------------------------
json Player::save() const {
  json j = Entity::save();
  j["name"] = name; j["skin"] = skin; j["mode"] = mode; j["flying"] = flying;
  j["x"] = pos.x; j["y"] = pos.y; j["z"] = pos.z; j["dim"] = jsFromDim(dimOfY(ifloor(pos.y)));
  j["health"] = hp; j["maxHealth"] = maxHp; j["xp"] = xp; j["level"] = level; j["hv"] = 2; j["heartCrystals"] = heartCrystals; j["air"] = air;
  j["inv"] = {{"main", slotsToJson(inv)}, {"armor", slotsToJson(armor)}, {"back", slotsToJson(back)}, {"off", slotsToJson(offhand)},
              {"hook", slotsToJson(hook)}, {"charm", slotsToJson(charm)}, {"cape", slotsToJson(cape)}, {"ender", slotsToJson(ender)}, {"selected", selected}};
  j["fuel"] = fuel;
  j["spawn"] = spawnPoint.y < 0 ? json() : json::array({spawnPoint.x, spawnPoint.y, spawnPoint.z});
  j["spawnDim"] = spawnPoint.y < 0 ? 0 : jsFromDim(dimOfY(ifloor(spawnPoint.y)));
  j["spawnRealm"] = spawnRealm;
  j["pocketReturn"] = pocketReturn; j["stats"] = stats; j["carrying"] = carrying; j["grave"] = grave;
  j["dead"] = dead;
  return j;
}
void Player::load(const json& j) {
  if (!j.is_object()) return;
  Entity::load(j);
  type = "player";
  auto num = [&](const char* k, float& v) { if (j.contains(k) && j[k].is_number()) v = j[k].get<float>(); };
  name = j.value("name", name); skin = j.value("skin", skin); mode = j.value("mode", mode); flying = j.value("flying", false);
  num("x", pos.x); num("y", pos.y); num("z", pos.z); num("yaw", yaw); num("pitch", pitch);
  num("health", hp); num("maxHealth", maxHp); num("fuel", fuel);
  xp = j.value("xp", 0); level = j.value("level", 0);
  if (j.contains("inv") && j["inv"].is_object()) {
    const json& o = j["inv"];
    slotsFromJson(o.value("main", json()), inv); slotsFromJson(o.value("armor", json()), armor); slotsFromJson(o.value("back", json()), back);
    slotsFromJson(o.value("off", json()), offhand); slotsFromJson(o.value("hook", json()), hook); slotsFromJson(o.value("charm", json()), charm);
    slotsFromJson(o.value("cape", json()), cape); slotsFromJson(o.value("ender", json()), ender);
    selected = clampv(o.value("selected", 0), 0, 8);
  } else {   // the engine foundation's first format
    if (j.contains("inv")) slotsFromJson(j["inv"], inv);
    if (j.contains("armor")) slotsFromJson(j["armor"], armor);
    if (j.contains("offhand")) slotsFromJson(j["offhand"], offhand);
    selected = clampv(j.value("selected", 0), 0, 8);
  }
  spawnPoint = vec3(0, -1, 0);
  if (j.contains("spawn") && j["spawn"].is_array() && j["spawn"].size() >= 3) spawnPoint = {j["spawn"][0], j["spawn"][1], j["spawn"][2]};
  else if (j.contains("spawnPoint") && j["spawnPoint"].is_array()) spawnPoint = {j["spawnPoint"][0], j["spawnPoint"][1], j["spawnPoint"][2]};
  spawnRealm = j.value("spawnRealm", 0);
  pocketReturn = j.value("pocketReturn", json());
  if (j.contains("stats") && j["stats"].is_object()) for (auto& [k, v] : j["stats"].items()) stats[k] = v;
  carrying = j.value("carrying", json());
  grave = j.value("grave", json());
  heartCrystals = clampv(j.value("heartCrystals", 0), 0, 10);
  air = j.contains("air") && j["air"].is_number() ? clampv(j["air"].get<float>(), 0.f, AIR_MAX) : AIR_MAX;
  // before 0.7, levels raised max health: those hearts come back as Heart Crystals
  if (j.value("hv", 0) != 2 && j.contains("health") && maxHp > 20) stats["refundHearts"] = (int)std::round((std::min(40.f, maxHp) - 20) / 2);
  maxHp = 0; recalcMax();
  hp = clampv(hp, 0.f, maxHp);
  if (hp <= 0) hp = maxHp;
  dead = false;
}

} // namespace dq
