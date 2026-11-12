// sim/drift.cpp — the way to Better than Adventure 8.0.1's Drift (REALM_DRIFT; its land: gen/drift.cpp).
//   The portal (BTA's BlockLogicBedrock / BlockLogicPortal: Blocks.PORTAL_DRIFT with GLOWSTONE as the frame and
//   BEDROCK as the trigger): build a frame of glowstone ("glow lantern") and set a block of bedrock on its bottom
//   inside — the frame fills with a silver portal. From there it is a portal like the Ember and Sky ones
//   (sim/portals.cpp PortalKind::Drift): a live window, walked through with no wait, its far end found or built on the
//   nearest land eight times as far out (BTA's worldScale 8) and linked both ways; break the frame and it goes out.
//   /tp drift goes the same way without a portal (and /tp <dimension> from there comes back where you left).
//   Its life: only chickens (BTA's BiomeDrift), kept a few round each person there.
#include "sim/gameplay.h"
#include "sim/creature.h"
#include "sim/player.h"
#include "sim/level.h"
#include "gen/generator.h"
#include "core/registry.h"
#include "world/world.h"
#include "common/noise.h"

namespace dq {

Entity* spawnCreature(Level& lv, const std::string& type, int realm, vec3 pos);   // sim/magic_combat.cpp

// the nearest land in the Drift to (x, z): rings out to 600 blocks (it hangs in great masses with sky between)
void driftLanding(Level& lv, double x, double z, int& outX, int& outZ) {
  World& dw = lv.world(REALM_DRIFT);
  DimGenerator* g = dw.gen ? dw.gen->forDim(DIM_OVERWORLD) : nullptr;
  int tx = (int)std::floor(x), tz = (int)std::floor(z), h = g ? g->spawnHeight(tx, tz) : -1;
  for (int r = 8; h < 0 && r < 600 && g; r += 8) for (int a = 0; a < 12 && h < 0; a++) {
    const int px = tx + (int)(std::cos(a * 0.5236f) * r), pz = tz + (int)(std::sin(a * 0.5236f) * r);
    const int hh = g->spawnHeight(px, pz); if (hh > 0) { tx = px; tz = pz; h = hh; }
  }
  outX = tx; outZ = tz;
}

namespace {
BlockId glowId() { static BlockId g = reg().blockId("glowstone"); return g; }
BlockId portalId() { static BlockId p = reg().blockId("drift_portal"); return p; }
BlockId bedrockId() { static BlockId b = reg().blockId("bedrock"); return b; }

// a glowstone frame round (x, y, z) — the bedrock on its bottom row, inside — along x (ax 0) or z (ax 1): fill it
bool tryCreate(Level& lv, World& w, int x, int y, int z) {
  const BlockId G = glowId(), P = portalId(), BR = bedrockId();
  if (!G || !P) return false;
  for (int ax = 0; ax < 2; ax++) {
    const int dx = ax == 0 ? 1 : 0, dz = ax == 0 ? 0 : 1;
    auto at = [&](int u, int v) { return w.getBlock(x + dx * u, y + v, z + dz * u); };
    auto inside = [&](BlockId b) { return b == AIR || b == BR || b == P || reg().tReplaceable[b]; };
    int lo = 0, hi = 0;
    while (lo > -22 && inside(at(lo - 1, 0))) lo--;
    while (hi < 22 && inside(at(hi + 1, 0))) hi++;
    if (at(lo - 1, 0) != G || at(hi + 1, 0) != G) continue;
    const int wdt = hi - lo + 1;
    if (wdt < 2 || wdt > 21) continue;
    int top = 0;
    while (top < 22 && inside(at(lo, top + 1))) top++;
    const int hgt = top + 1;
    if (hgt < 3 || hgt > 21 || at(lo, top + 1) != G) continue;
    bool ok = true;
    for (int u = lo; u <= hi && ok; u++) { ok = at(u, -1) == G && at(u, top + 1) == G; for (int v = 0; v <= top && ok; v++) ok = inside(at(u, v)); }
    for (int v = 0; v <= top && ok; v++) ok = at(lo - 1, v) == G && at(hi + 1, v) == G;
    if (!ok) continue;
    for (int u = lo; u <= hi; u++) for (int v = 0; v <= top; v++) w.setBlock(x + dx * u, y + v, z + dz * u, P, (u8)ax, SetFlags::Urgent);
    lv.fx->sound("portal", vec3(x, y, z) + vec3(0.5f), 24);
    lv.fx->particles("portal", vec3(x, y + 1, z) + vec3(0.5f), 30);
    for (auto& e : lv.entities()) if (e->isPlayer() && !e->removed && e->realm == w.realm && glm::distance(e->pos, vec3(x, y, z)) < 16) lv.fx->message(e->id, "The way to the Drift opens", "rare");
    return true;
  }
  return false;
}

struct DriftSystem : LevelSystem {
  explicit DriftSystem(Level& l) : lv(&l) {}
  Level* lv;
  double chickT = 0;
  const char* name() const override { return "drift"; }

  void place(Player& p, int realm, vec3 at) {
    if (p.riding) if (auto* m = dynamic_cast<Creature*>(lv->entity(p.riding))) dismount(*lv, *m, p);
    lv->world(realm);
    p.realm = realm; p.pos = at; p.prevPos = at; p.vel = vec3(0); p.hasFallStart = false; p.fallDist = 0;
    lv->fx->sound("portal", at + vec3(0, 1, 0), 24);
  }
  // /tp: in, onto the nearest land eight times out; out, back where you went in
  void travel(Player& p) {
    if (p.realm != REALM_DRIFT) {
      p.data["driftReturn"] = {{"realm", p.realm}, {"x", p.pos.x}, {"y", p.pos.y}, {"z", p.pos.z}};
      int tx = 0, tz = 0;
      driftLanding(*lv, p.pos.x * 8, p.pos.z * 8, tx, tz);
      World& dw = lv->world(REALM_DRIFT);
      DimGenerator* g = dw.gen ? dw.gen->forDim(DIM_OVERWORLD) : nullptr;
      int h = g ? g->spawnHeight(tx, tz) : -1;
      if (h < 0) h = 140;
      for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) dw.requestChunk(DIM_OVERWORLD, (tx >> 4) + dx, (tz >> 4) + dz, 1000);
      place(p, REALM_DRIFT, vec3(tx + 0.5f, (float)(dimBase(DIM_OVERWORLD) + h + 1), tz + 0.5f));
      lv->fx->message(p.id, "The Drift", "rare");
    } else {
      json r = p.data.value("driftReturn", json());
      vec3 at = r.is_object() ? vec3(r.value("x", 0.f), r.value("y", 0.f), r.value("z", 0.f)) : vec3(lv->info.spawn.x, lv->info.spawn.y < 0 ? dimBase(DIM_OVERWORLD) + 90.f : lv->info.spawn.y, lv->info.spawn.z);
      int realm = r.is_object() ? r.value("realm", (int)REALM_MAIN) : REALM_MAIN;
      if (realm == REALM_DRIFT) realm = REALM_MAIN;
      place(p, realm, at);
    }
  }
  void tick(Level& l, double dt) override {
    if (l.isRemote() || (chickT += dt) < 5) return;
    chickT = 0;
    // the Drift's chickens: a few round each person there
    for (auto& e : l.entities()) {
      auto* p = dynamic_cast<Player*>(e.get());
      if (!p || p->removed || p->dead || p->realm != REALM_DRIFT) continue;
      int n = 0;
      for (auto& c : l.entities()) if (c->realm == REALM_DRIFT && c->type == "chicken" && !c->removed && glm::distance(c->pos, p->pos) < 64) n++;
      if (n >= 6) continue;
      World& w = l.world(REALM_DRIFT);
      const float a = (float)(hash32((int)p->pos.x, (int)l.info.time, n, 7) % 628) / 100.f, r = 18 + (float)(hash32(n, (int)l.info.time, 3, 9) % 22);
      const int x = ifloor(p->pos.x + std::cos(a) * r), z = ifloor(p->pos.z + std::sin(a) * r);
      for (int y = ifloor(p->pos.y) + 24; y > ifloor(p->pos.y) - 40; y--) {
        if (!w.isLoaded(x, y, z)) break;
        const BlockId b = w.getBlock(x, y, z);
        if (b == AIR) continue;
        if (b == reg().blockId("grass") && w.getBlock(x, y + 1, z) == AIR) spawnCreature(l, "chicken", REALM_DRIFT, vec3(x + 0.5f, y + 1.f, z + 0.5f));
        break;
      }
    }
  }
};
}

// /tp drift (and /tp <dimension> from the Drift)
std::string driftTravel(Level& lv, Player& p) {
  DriftSystem* d = lv.system<DriftSystem>();
  if (!d) return "The Drift isn't open in this world";
  const bool in = p.realm != REALM_DRIFT;
  d->travel(p);
  return in ? "To the Drift" : "Back from the Drift";
}

void registerDrift() {
  static bool done = false;
  if (done) return;
  done = true;
  // BTA: bedrock set on glowstone in a glowstone frame lights it (the portal system does the rest: sim/portals.cpp)
  hooks().blockChanged.push_back([](Level& lv, World& w, int x, int y, int z, BlockId, BlockId newId) {
    if (lv.isRemote() || !newId || newId != bedrockId() || w.getBlock(x, y - 1, z) != glowId()) return;
    tryCreate(lv, w, x, y, z);
  });
  registerLevelSystem([](Level& lv) -> std::shared_ptr<LevelSystem> { return std::make_shared<DriftSystem>(lv); });
}

} // namespace dq
