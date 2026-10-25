// sim/haunt.cpp — game/haunt.js: the watcher. A figure in the default skin with white, pupil-less eyes. He never
// attacks and never follows: he stands at the edge of what you can see (on a ridge, half behind a tree, at the far
// end of a cave, right behind you) and when you look straight at him, or come close, he is simply gone. He leaves
// things behind, always out of sight: 2x2 tunnels lit with redstone torches, signs, trees stripped of every leaf, a
// torch swapped for a redstone one. Rare: a glimpse every half hour or so. The "haunting" game rule sends him away.
// Sightings are for the players on this machine (not ghosts of remote players); traces are the host's.
#include "sim/endstory_int.h"
#include "sim/portals.h"
#include "sim/commands.h"
#include "sim/weather.h"

namespace dq::es {

static const std::vector<std::vector<std::string>> SIGNS = {{"", "STOP", "", ""}, {"I see you", "", "", ""}, {"", "Wake up", "", ""}, {"Go back", "", "", ""}, {"", "", "Nobody is", "here"}, {"Too far", "", "", ""},
  {"", "It is too late", "", ""}, {"Leave the", "tunnels alone", "", ""}, {"", "Behind you", "", ""}, {"Do not dig", "down", "", ""}, {"", "Still here", "", ""}, {"He was here", "first", "", ""}};
// (riftarc.js) once the boundary thins, his signs say other things
static const std::vector<std::vector<std::string>> RIFT_SIGNS = {{"WE ARE", "WATCHING", "", ""}, {"", "IT BREAKS", "", ""}, {"The keys", "are mine", "", ""}, {"", "Do not", "open it", ""}};

// what his tunnels may cut through (never anything built)
static bool hauntRock(BlockId id) {
  static std::vector<BlockId> R;
  if (R.empty()) for (const char* k : {"stone", "dirt", "grass", "gravel", "andesite", "diorite", "granite", "slate", "tuff", "deepslate", "coarse_dirt", "sandstone", "clay", "coal_ore", "iron_ore", "copper_ore", "tin_ore",
      "snowy_grass", "podzol", "calcite", "limestone", "marble", "dripstone_block", "mud", "red_sandstone", "terracotta", "emberstone", "basalt", "blackstone", "ash", "skystone", "sky_grass", "end_stone", "desert_stone", "desert_sandstone"})
    if (BlockId b = B(k)) R.push_back(b);
  return id && std::find(R.begin(), R.end(), id) != R.end();
}
static bool rulesHaunting(Level& lv) { const json& R = lv.info.rules; return !(R.is_object() && R.contains("haunting") && R["haunting"] == false); }

std::vector<ivec3> hauntPortals(Level& lv, int realm) {
  std::vector<ivec3> out;
  PortalSystem* ps = portalSystem(lv);
  if (!ps || !lv.worldIfOpen(realm)) return out;
  for (const PortalWindow& w : ps->windows(realm)) if (w.kind == PortalKind::Ember || w.kind == PortalKind::Sky)
    for (const ivec3& c : w.cells) if (c.y == w.lo.y) out.push_back(c);   // the bottom of each column of portal
  return out;
}

bool hauntBossFight(Level& lv) {
  auto ps = livePlayers(lv);
  for (auto& u : lv.entities()) if (auto* c = dynamic_cast<Creature*>(u.get()))
    if (c->T->boss && c->alive() && !c->removed && !c->tamed && c->owner.empty())
      for (Player* q : ps) if (q->realm == c->realm && glm::distance(q->pos, c->pos) < 40) return true;
  return false;
}

static float eyeOf(const Entity& e) { return e.isPlayer() ? EYE : e.h * 0.9f; }
static bool seenBy(Level& lv, const Entity& q, vec3 spot) {   // Watcher.lineOfSight from q's eyes to a figure standing at spot
  vec3 eye = q.pos + vec3(0, eyeOf(q), 0), head = spot + vec3(0, 1.6f, 0);
  return lineOfSight(lv, q.realm, eye, head, glm::distance(eye, head));
}

// ------------------------------------------------------------------ the figure
class Watcher : public Entity {
public:
  Watcher() { type = "watcher"; w = 0.3f; h = 1.8f; netLocal = true; noGravity = true; }
  bool live = false;               // made this session (a saved one is cleared at once)
  std::string mode = "far";
  float life = 40, lookT = 0, hiddenT = 0, leaving = 0, walk = 0, stare = 0, lookLimit = -1;
  bool asBoar = false, riftDoor = false;
  double born = 0;
  u32 viewer = 0;
  Player* nearest(Level& lv) {
    Player* best = nullptr; float bd = 1e9f;
    for (Player* q : livePlayers(lv, realm)) if (!q->ghost) { float d = std::hypot(q->pos.x - pos.x, q->pos.z - pos.z); if (d < bd) { bd = d; best = q; } }
    return best;
  }
  void faceAt(const Entity& p) { yaw = std::atan2(-(p.pos.x - pos.x), -(p.pos.z - pos.z)); }
  void tick(Level& lv, double dtd) override {
    float dt = (float)dtd;
    data["walk"] = walk; data["leaving"] = leaving; data["boar"] = asBoar;   // for the drawing
    Player* p = live ? nearest(lv) : nullptr;
    if (!p || p->dead || !hauntDimOk(lv, *p) || !rulesHaunting(lv)) { removed = true; return; }
    data["look"] = p->id;
    // a stare lasts as long as it lasts, then he goes (riftarc.js)
    if (stare > 0) { stare -= dt; faceAt(*p); return; }
    vec3 eye = p->pos + vec3(0, EYE, 0), dd = pos + vec3(0, 1.6f, 0) - eye;
    float d = glm::length(dd);
    if (leaving > 0) {
      // walk away, then gone
      leaving -= dt;
      pos.x -= std::sin(yaw) * 2.6f * dt; pos.z -= std::cos(yaw) * 2.6f * dt;
      walk += dt * 7;
      if (leaving <= 0) watcherVanish(lv, *this);
      return;
    }
    faceAt(*p);
    // in a doorway (or stepped out of a rift) he lets you come close, the way the tale has it
    if (d < (mode == "behind" ? 5 : mode == "peek" ? 2.5f : 11)) { watcherVanish(lv, *this); return; }
    // is the player looking at him?
    vec3 lk = lookVec(*p);
    float dot = glm::dot(lk, dd) / std::max(d, 1e-3f), straight = std::cos(mode == "behind" ? 0.45f : 0.12f);
    bool seen = lineOfSight(lv, realm, eye, pos + vec3(0, 1.6f, 0), d);
    EndStory* S = endStory(lv);
    if (seen && dot > 0.55f && S) S->haunt.noticed = true;   // in view: they've seen him, even from the corner of their eye
    if (seen && dot > straight) {
      lookT += dt;
      if (lookLimit < 0) lookLimit = mode == "behind" ? 0.08f : mode == "peek" ? 3 + rnd() * 1.2f : 0.7f + rnd() * 0.8f;
      if (lookT > lookLimit) {
        if (mode == "far" && rnd() < 0.3f && !asBoar) { leaving = 1.1f; yaw += PI; }
        else watcherVanish(lv, *this);
        return;
      }
    }
    // standing behind you: now and then a footstep
    if (mode == "behind" && rnd() < dt * 0.6f) lv.fx->sound("cave_steps", pos, 30);
    life -= dt;
    if (life <= 0) { watcherVanish(lv, *this); return; }
    if (!seen) { hiddenT += dt; if (hiddenT > 14) watcherVanish(lv, *this); } else hiddenT = 0;
  }
};

Entity* spawnWatcher(Level& lv, Player& p, vec3 at, const std::string& mode, float stare, bool riftDoor) {
  auto w = std::make_unique<Watcher>();
  w->realm = p.realm; w->pos = w->prevPos = at; w->live = true; w->mode = mode; w->stare = stare; w->riftDoor = riftDoor;
  w->life = 25 + rnd() * 35; w->asBoar = mode == "far" && rnd() < 0.08f; w->viewer = p.id;
  if (EndStory* S = endStory(lv)) w->born = S->now;
  w->faceAt(p);
  w->data = {{"mode", mode}, {"boar", w->asBoar}, {"look", p.id}};
  Entity* e = lv.spawn(std::move(w));
  if (EndStory* S = endStory(lv)) S->haunt.active = e->id;
  return e;
}

void watcherVanish(Level& lv, Entity& we) {
  auto* w = dynamic_cast<Watcher*>(&we);
  if (!w) { we.removed = true; return; }
  EndStory* S = endStory(lv);
  // (riftarc.js) his vanishing, once the boundary thins: a whisper of static and the lights flickering
  if (riftStability(lv) < 75 && !w->removed) {
    lv.fx->sound("rift_static", w->pos + vec3(0, 1, 0));
    if (S) S->dreadT = std::max(S->dreadT, 0.5);
    sfx::burst(lv, w->pos + vec3(0, 0.9f, 0), "mana_arcane", 20, 0.6f, json{{"life", 0.6f}, {"size", 0.06f}, {"spread", 0.6f}});
  }
  w->removed = true;
  if (!S || !S->haunt.noticed) return;
  json& f = sub(lv.info.data, "flags");
  int seen = jint(f, "watcherSeen") + 1; f["watcherSeen"] = seen;
  u32 who = w->viewer;
  if (rnd() < 0.45f) lv.fx->soundUi(who, rnd() < 0.5f ? "cave" : "cave_breath");
  // the old story: someone who isn't there, telling you to stop
  if (seen >= 3 && !truthy(f, "watcherSpoke") && rnd() < 0.5f) {
    f["watcherSpoke"] = true;
    S->later(2.5 + rnd() * 4, [who](Level& l) { l.fx->message(who, "stop", "whisper"); });
  }
}

// ------------------------------------------------------------------ where he stands
static Player* hauntPlayer(Level& lv) {   // g.player: the first one at this machine
  for (Player* p : levelPlayers(lv)) if (!p->ghost && !p->removed) return p;
  return nullptr;
}
static bool portalSpot(Level& lv, Player& p, float minD, vec3& out) {
  World& w = lv.world(p.realm);
  std::vector<ivec3> ports;
  for (ivec3 q : hauntPortals(lv, p.realm)) if (std::hypot(q.x - p.pos.x, q.z - p.pos.z) < 90) ports.push_back(q);
  if (ports.empty()) return false;
  bool hasFb = false; vec3 fb(0);
  BlockId ep = B("ember_portal"), sp = B("sky_portal");
  for (int i = 0; i < 30; i++) {
    ivec3 q = ports[(size_t)(rnd() * ports.size()) % ports.size()]; int d = (int)(rnd() * 4) & 3, k = 1 + (int)(rnd() * 2);
    int x = q.x + DIRX[d] * k, z = q.z + DIRZ[d] * k;
    BlockId b = w.getBlock(x, q.y, z); if (b == ep || b == sp) continue;
    int y = standY(w, x + 0.5f, z + 0.5f, (float)q.y);
    if (y < 0 || y > q.y + 1 || y < q.y - 2 || std::hypot(x + 0.5f - p.pos.x, z + 0.5f - p.pos.z) < minD) continue;   // beside it, not up on its frame
    vec3 spot(x + 0.5f, y, z + 0.5f);
    if (seenBy(lv, p, spot)) { out = spot; return true; }
    if (!hasFb) { hasFb = true; fb = spot; }
  }
  if (hasFb) out = fb;
  return hasFb;
}
static bool findSpot(Level& lv, Player& p, const std::string& mode, vec3& out) {
  if (p.realm == REALM_POCKET) return portalSpot(lv, p, 5, out);
  World& w = lv.world(p.realm);
  int dim = dimHere(p) < 0 ? DIM_OVERWORLD : dimHere(p), base = dimBase(dim);
  float far = 46;
  for (int i = 0; i < 24; i++) {
    float side = rnd() < 0.5f ? -1.f : 1.f, ang, d;
    if (mode == "far") { ang = p.yaw + side * (0.35f + rnd() * 0.45f); d = 18 + rnd() * (far - 18); }   // 20-45 degrees off where you look
    else if (mode == "peek") { ang = p.yaw + side * (0.2f + rnd() * 0.6f); d = 14 + rnd() * 14; }
    else if (mode == "behind") { ang = p.yaw + PI + (rnd() - 0.5f) * 0.8f; d = 8 + rnd() * 5; }
    else { ang = p.yaw + (rnd() - 0.5f) * 2.2f; d = 12 + rnd() * 16; }
    float x = std::floor(p.pos.x - std::sin(ang) * d) + 0.5f, z = std::floor(p.pos.z - std::cos(ang) * d) + 0.5f;
    int ix = ifloor(x), iz = ifloor(z);
    if (!w.isLoaded(ix, ifloor(p.pos.y), iz)) continue;
    int y = -1;
    if (mode == "cave" || mode == "behind") {
      for (int yy = ifloor(p.pos.y) + 5; yy > p.pos.y - 8; yy--) if (solid(w.getBlock(ix, yy - 1, iz)) && !w.getBlock(ix, yy, iz) && !w.getBlock(ix, yy + 1, iz)) { y = yy; break; }
      if (mode == "cave" && y > 0 && (w.getLight(ix, y, iz) & 15) > 6) continue;   // stays in the dark
    } else {
      y = findSafeY(w, ix, iz, dim, std::min(base + DIM_H - 3, ifloor(p.pos.y) + 30));
      if (y > 0 && isLeaves(w.getBlock(ix, y - 1, iz))) continue;
      // peeking: right beside a tree trunk
      if (mode == "peek" && y > 0) {
        bool trunk = false;
        for (int k = 0; k < 4; k++) if (isLog(w.getBlock(ix + DIRX[k], y + 1, iz + DIRZ[k]))) trunk = true;
        if (!trunk) continue;
      }
    }
    if (y < 0 || std::fabs(y - p.pos.y) > 22) continue;
    if (mode != "behind" && !seenBy(lv, p, vec3(x, y, z))) continue;
    out = vec3(x, y, z);
    return true;
  }
  return false;
}

static void hauntUpdate(Level& lv, EndStory& S, double dt);

// ------------------------------------------------------------------ what he leaves behind
// somewhere loaded, minD..maxD from every player, that none of them can see
static bool unseenSpot(Level& lv, Player& p, float minD, float maxD, ivec3& out) {
  World& w = lv.world(p.realm);
  int dim = dimHere(p) < 0 ? DIM_OVERWORLD : dimHere(p), base = dimBase(dim);
  auto ps = livePlayers(lv, p.realm);
  for (int i = 0; i < 20; i++) {
    float a = rnd() * TAU, d = minD + rnd() * (maxD - minD);
    int x = ifloor(p.pos.x + std::cos(a) * d), z = ifloor(p.pos.z + std::sin(a) * d), py = ifloor(p.pos.y);
    if (!w.isLoaded(x, py, z) || !w.isLoaded(x + 16, py, z) || !w.isLoaded(x - 16, py, z) || !w.isLoaded(x, py, z + 16) || !w.isLoaded(x, py, z - 16)) continue;
    bool close = false; for (Player* q : ps) if (std::hypot(q->pos.x - x, q->pos.z - z) < minD) close = true;
    if (close) continue;
    int y = findSafeY(w, x, z, dim, std::min(base + DIM_H - 3, py + 40));
    if (y < 0) continue;
    bool seen = false; for (Player* q : ps) if (seenBy(lv, *q, vec3(x + 0.5f, y, z + 0.5f))) seen = true;
    if (seen) continue;
    out = ivec3(x, y, z);
    return true;
  }
  return false;
}
// a tunnel bored straight into a hillside or a cave wall (2 tall, usually 1 wide), a redstone torch every six blocks
static bool digTunnel(Level& lv, Player& p) {
  World& w = lv.world(p.realm);
  int base = dimBase(dimHere(p) < 0 ? DIM_OVERWORLD : dimHere(p));
  BlockId torch = B("redstone_torch");
  for (int tries = 0; tries < 30; tries++) {
    ivec3 s; if (!unseenSpot(lv, p, 30, 64, s)) return false;
    int sx = s.x, sz = s.z, sy = rnd() < 0.5f ? s.y : std::max(base + 8, s.y - 6 - (int)std::floor(rnd() * 30));
    int dir = (int)(rnd() * 4) & 3, dx = DIRX[dir], dz = DIRZ[dir], px = -dz, pz = dx;   // along, and across
    // the mouth must be open air with ground under it, and solid rock ahead
    if (w.getBlock(sx, sy, sz) != AIR || w.getBlock(sx, sy + 1, sz) != AIR || !solid(w.getBlock(sx, sy - 1, sz))) continue;
    auto rock = [&](int x, int y, int z) { return hauntRock(w.getBlock(x, y, z)); };
    int ok = 0;
    for (int k = 1; k <= 8; k++) if (rock(sx + dx * k, sy, sz + dz * k) && rock(sx + dx * k, sy + 1, sz + dz * k) && rock(sx + dx * k + px, sy, sz + dz * k + pz)) ok++;
    if (ok < 7) continue;
    int len = 12 + (int)std::floor(rnd() * 18), wide = rnd() < 0.3f ? 2 : 1;   // mostly 1 wide and 2 tall, now and then 2x2
    int made = 0;
    for (int k = 1; k <= len; k++) {
      int x = sx + dx * k, z = sz + dz * k;
      // stop at anything built, at water, or where the rock runs out
      bool stop = false;
      for (int a = 0; a < wide; a++) for (int hh = 0; hh < 2; hh++) { BlockId id = w.getBlock(x + px * a, sy + hh, z + pz * a); if (id && !hauntRock(id)) stop = true; }
      if (stop || liquid(w.getBlock(x, sy + 2, z))) break;
      for (int a = 0; a < wide; a++) for (int hh = 0; hh < 2; hh++) w.setBlock(x + px * a, sy + hh, z + pz * a, AIR, 0);
      made++;
      // a redstone torch on the wall every six blocks
      if (k % 6 == 3 && rock(x - px, sy + 1, z - pz)) {
        u8 meta = px == 1 ? 1 : px == -1 ? 2 : pz == 1 ? 3 : 4;   // facing away from the wall at -across
        w.setBlock(x, sy + 1, z, torch, meta);
      }
    }
    if (made >= 8) return true;
  }
  return false;
}
static void writeSign(Level& lv, World& w, ivec3 at, const Player& p) {
  BlockId sign = B("oak_sign"); if (!sign) sign = B("spruce_sign");
  float yaw = std::atan2(p.pos.x - at.x, p.pos.z - at.z);                 // turned toward where you are
  int rot = (int)std::round(((PI - yaw) / TAU) * 16) & 15;
  w.setBlock(at.x, at.y, at.z, sign, (u8)rot);
  const auto& L = riftStability(lv) < 75 ? RIFT_SIGNS : SIGNS;
  const auto& lines = L[(size_t)(rnd() * L.size()) % L.size()];
  w.setBlockEntity(at.x, at.y, at.z, json{{"type", "sign"}, {"lines", lines}});
}
// a sign with a few words on it, facing the way you'll come
static bool placeSign(Level& lv, Player& p) {
  World& w = lv.world(p.realm);
  ivec3 s; if (!unseenSpot(lv, p, 25, 55, s)) return false;
  if (w.getBlock(s.x, s.y, s.z) != AIR || !solid(w.getBlock(s.x, s.y - 1, s.z)) || liquid(w.getBlock(s.x, s.y - 1, s.z))) return false;
  if (!B("oak_sign") && !B("spruce_sign")) return false;
  writeSign(lv, w, s, p);
  return true;
}
// a tree with every leaf gone, standing among green ones
static bool stripTree(Level& lv, Player& p) {
  World& w = lv.world(p.realm);
  int dim = dimHere(p) < 0 ? DIM_OVERWORLD : dimHere(p), base = dimBase(dim);
  static const char* builtK[] = {"plank", "brick", "glass", "cobble", "wool", "door", "torch", "chest"};
  for (int tries = 0; tries < 12; tries++) {
    ivec3 s; if (!unseenSpot(lv, p, 30, 60, s)) return false;
    for (int dz = -6; dz <= 6; dz++) for (int dx = -6; dx <= 6; dx++) {
      int x = s.x + dx, z = s.z + dz;
      int top = findSafeY(w, x, z, dim, base + DIM_H - 3) - 1;
      if (top < 0 || !isLeaves(w.getBlock(x, top, z))) continue;
      int y = top; while (y > base && isLeaves(w.getBlock(x, y, z))) y--;
      if (!isLog(w.getBlock(x, y, z))) continue;
      // only a wild tree: nothing built around it
      bool built = false;
      for (int k = 0; k < 40 && !built; k++) {
        BlockId id = w.getBlock(x + (int)std::floor(rnd() * 9) - 4, y - 3 + (int)std::floor(rnd() * 6), z + (int)std::floor(rnd() * 9) - 4);
        if (!id) continue;
        if (def(id).render == Render::Shape) built = true;
        for (const char* k2 : builtK) if (def(id).key.find(k2) != std::string::npos) built = true;
      }
      if (built) continue;
      int n = 0;
      for (int yy = y - 2; yy <= top + 2; yy++) for (int ez = -4; ez <= 4; ez++) for (int ex = -4; ex <= 4; ex++) if (isLeaves(w.getBlock(x + ex, yy, z + ez))) { w.setBlock(x + ex, yy, z + ez, AIR, 0); n++; }
      if (n > 8) return true;
    }
  }
  return false;
}
// a torch left burning in the dark becomes a redstone torch while you're away
static bool swapTorch(Level& lv, Player& p) {
  World& w = lv.world(p.realm);
  BlockId torch = B("torch"), rs = B("redstone_torch");
  auto ps = livePlayers(lv, p.realm);
  int pcx = ifloor(p.pos.x) >> 4, pcz = ifloor(p.pos.z) >> 4;
  for (auto& [k, cp] : w.chunks()) {
    Chunk& c = *cp;
    if (std::abs(c.cx - pcx) > 6 || std::abs(c.cz - pcz) > 6) continue;
    for (int sy = 0; sy < SECS; sy++) {
      const Section* sec = c.section(sy);
      if (!sec) continue;
      for (int i = 0; i < 4096; i++) {
        if (sec->blocks[i] != torch) continue;
        int x = c.worldX() + (i & 15), y = c.baseY() + sy * 16 + (i >> 8), z = c.cz * 16 + ((i >> 4) & 15);
        bool near = false; for (Player* q : ps) if (std::hypot(q->pos.x - x, q->pos.z - z) < 24) near = true;
        if (near) continue;
        if ((w.getLight(x, y, z) >> 4) > 3 || rnd() < 0.7f) continue;   // only down in the dark
        bool wired = false;
        for (int dy = -2; dy <= 2 && !wired; dy++) for (int dz = -2; dz <= 2 && !wired; dz++) for (int dx = -2; dx <= 2; dx++) if (def(w.getBlock(x + dx, y + dy, z + dz)).has("rs")) { wired = true; break; }
        if (wired) continue;
        bool seen = false; for (Player* q : ps) if (seenBy(lv, *q, vec3(x + 0.5f, y, z + 0.5f))) seen = true;
        if (seen) continue;
        w.setBlock(x, y, z, rs, sec->meta[i]);
        return true;
      }
    }
  }
  return false;
}
static bool leaveTrace(Level& lv, Player& p) {
  if (lv.isRemote()) return false;   // (0.9.0.54) the world is the host's to change
  json& f = sub(lv.info.data, "flags");
  bool done = false;
  if (p.realm == REALM_POCKET) {
    // (0.9.0.50) in a pocket dimension he leaves only small things, by the portal he came through
    if (hauntPortals(lv, p.realm).empty()) return false;
    if (rnd() < 0.6f) {
      vec3 s; World& w = lv.world(p.realm);
      if (portalSpot(lv, p, 12, s)) {
        ivec3 at(ifloor(s.x), ifloor(s.y), ifloor(s.z));
        if (!seenBy(lv, p, s) && w.getBlock(at.x, at.y, at.z) == AIR && solid(w.getBlock(at.x, at.y - 1, at.z)) && (B("oak_sign") || B("spruce_sign"))) { writeSign(lv, w, at, p); done = true; }
      }
    } else done = swapTorch(lv, p);
  } else {
    float r = rnd();
    done = r < 0.4f ? digTunnel(lv, p) : r < 0.65f ? placeSign(lv, p) : r < 0.85f ? stripTree(lv, p) : swapTorch(lv, p);
  }
  if (done) f["watcherTraces"] = jint(f, "watcherTraces") + 1;
  return done;
}

// ------------------------------------------------------------------ the haunting
static void hauntUpdate(Level& lv, EndStory& S, double dt) {
  auto& H = S.haunt;
  if (!H.init) { H.init = true; H.next = 600 + rnd() * 900; H.traceT = 900 + rnd() * 900; }
  Player* p = hauntPlayer(lv);
  if (!p || !hauntDimOk(lv, *p) || !rulesHaunting(lv)) return;
  if (playTime(lv) < 120 && !H.force) return;
  // his traces, left where you aren't looking (any player here: the host keeps the world)
  H.traceT -= dt;
  if (H.traceT <= 0) {
    auto ps = livePlayers(lv);
    std::vector<Player*> c; for (Player* q : ps) if (hauntDimOk(lv, *q)) c.push_back(q);
    bool ok = !c.empty() && leaveTrace(lv, *c[(size_t)(rnd() * c.size()) % c.size()]);
    H.traceT = ok ? 1200 + rnd() * 1500 : 60 + rnd() * 60;
  }
  if (p->dead) return;
  // (0.9.0.47) one left behind (out where the land stopped loading) is gone after a while
  Entity* a = H.active ? lv.entity(H.active) : nullptr;
  if (auto* wa = dynamic_cast<Watcher*>(a); wa && !wa->removed && S.now - wa->born > 90) wa->removed = true;
  if (!a || a->removed) { H.active = 0; a = nullptr; }
  if (a) return;
  H.next -= dt;
  if (H.next > 0) return;
  // dusk, night and deep caves make him bolder
  int sky = lv.world(p->realm).getLight(ifloor(p->pos.x), ifloor(p->pos.y + EYE), ifloor(p->pos.z)) >> 4;
  bool underground = sky < 5;
  WeatherSystem* W = weatherOf(lv);
  float mist = W ? W->mist : 0;
  int dim = dimHere(*p);
  float chance = (underground ? 0.5f : envDay(lv, dim < 0 ? DIM_OVERWORLD : dim) < 0.5f ? 0.6f : 0.4f) + std::min(0.3f, mist * 0.4f);   // (0.9.0.50) and fog
  if (!H.force && (hauntBossFight(lv) || rnd() > chance)) { H.next = 120 + rnd() * 240; return; }
  float r = rnd();
  std::string mode = underground ? (r < 0.65f ? "cave" : "behind") : (r < 0.55f ? "far" : r < 0.8f ? "peek" : "behind");
  vec3 spot;
  if (!findSpot(lv, *p, mode, spot)) { H.next = 15 + rnd() * 25; return; }
  H.noticed = false;
  Entity* w = spawnWatcher(lv, *p, spot, mode);
  H.force = false;
  H.next = 1200 + rnd() * 1500;
  // (and a portal's hum as he steps out of one, now and then)
  if (p->realm == REALM_POCKET && rnd() < 0.5f) lv.fx->sound("portal", w->pos + vec3(0, 1, 0), 24);
}

void hauntTick(Level& lv, EndStory& S, double dt) {
  // the haunting grows bolder as the boundary thins; once sealed, he's gone (riftarc.js)
  const json& R = riftRec(lv);
  if (truthy(R, "sealed")) return;
  int st = riftStability(lv);
  hauntUpdate(lv, S, dt * (st < 15 ? 3 : st < 40 ? 2.2 : st < 75 ? 1.6 : 1));
}

static bool forceSighting(Level& lv, Player& p) {
  EndStory* S = endStory(lv); if (!S) return false;
  Entity* a = S->haunt.active ? lv.entity(S->haunt.active) : nullptr;
  if (a && !a->removed) return true;
  if (!hauntDimOk(lv, p)) return false;
  S->haunt.force = true;
  for (int i = 0; i < 6; i++) {
    S->haunt.next = 0;
    hauntUpdate(lv, *S, 0.01);
    Entity* b = S->haunt.active ? lv.entity(S->haunt.active) : nullptr;
    if (b && !b->removed) break;
  }
  S->haunt.force = false;
  Entity* b = S->haunt.active ? lv.entity(S->haunt.active) : nullptr;
  return b && !b->removed;
}

void registerHaunt() {
  registerEntityFactory("watcher", [](const std::string&) { return std::make_unique<Watcher>(); });
  // /herobrine is the gameplay command list's (sim/gamecommands.cpp): it emits "herobrine" [name, "trace"|""], answered
  // in sim/endstory.cpp; here it is only kept out of every list
  hideChatCommand("herobrine");
}

} // namespace dq::es

namespace dq {
bool hauntDimOk(Level& lv, const Player& p) {
  if (p.realm == REALM_HIS) return false;
  if (p.realm == REALM_POCKET) return !es::hauntPortals(lv, REALM_POCKET).empty();   // he walks in through a portal its owner built
  return true;
}
bool hauntForceSighting(Level& lv, Player& p) { return es::forceSighting(lv, p); }
bool hauntLeaveTrace(Level& lv, Player& p) { return es::leaveTrace(lv, p); }
}
