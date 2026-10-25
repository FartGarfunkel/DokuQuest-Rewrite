// sim/fishing.cpp — the float, what bites, casting and reeling in (game/fishing.js).
#include "sim/fishing.h"
#include <cmath>

namespace dq {

namespace {
std::unordered_map<u32, u32>& floats() { static std::unordered_map<u32, u32> m; return m; }      // player id -> float id
std::unordered_map<u32, float>& winds() { static std::unordered_map<u32, float> m; return m; }    // player id -> castWind
bool isRod(const ItemStack& s) { return !s.empty() && (s.id == "fishing_rod" || s.id == "fishing_rod_cast"); }
bool water(Level& lv, int realm, int x, int y, int z) { return reg().tLiquid[lv.world(realm).getBlock(x, y, z)] == 1; }
const char* FISH[] = {"cod", "salmon", "tropical_fish", "pufferfish"};
bool isFish(const std::string& t) { for (auto f : FISH) if (t == f) return true; return false; }
float heading(float x0, float z0, float x1, float z1) { return std::atan2(-(x1 - x0), -(z1 - z0)); }
}

const int* fishSizes(const std::string& id) {
  static const int cod[2] = {30, 70}, salmon[2] = {45, 90}, trop[2] = {8, 25}, puff[2] = {15, 40}, other[2] = {20, 50};
  return id == "cod" ? cod : id == "salmon" ? salmon : id == "tropical_fish" ? trop : id == "pufferfish" ? puff : other;
}
static int sizeOf(const std::string& id) { const int* s = fishSizes(id); return (int)std::lround(s[0] + std::pow(frand(), 1.6f) * (s[1] - s[0])); }

// ---- what bites
FishCatch fishingCatch(Level& lv, int realm, vec3 at) {
  float r = frand();
  World& w = lv.world(realm);
  int bx = ifloor(at.x), bz = ifloor(at.z), dim = realm == REALM_MAIN ? dimOfY(ifloor(at.y)) : DIM_OVERWORLD;
  // the water's temperature (the chunk's climate) and whether this is the open sea (deep water at sea level)
  float temp = 0; bool ocean = false;
  if (const Chunk* c = w.chunk(dim, bx >> 4, bz >> 4)) {
    temp = (c->climate[((bz & 15) * 16 + (bx & 15)) * 2] - 128) / 127.f;
    int depth = 0;
    for (int y = ifloor(at.y); y > dimBase(dim) && water(lv, realm, bx, y, bz); y--) depth++;
    ocean = depth >= 8 && ifloor(at.y) - dimBase(dim) <= SEA_LEVEL + 1;
  }
  auto pick = [](std::vector<std::pair<const char*, float>> l) -> std::string {
    std::vector<std::pair<const char*, float>> ok;
    for (auto& e : l) if (reg().item(e.first)) ok.push_back(e);
    float t = 0; for (auto& e : ok) t += e.second;
    float q = frand() * t;
    for (auto& e : ok) { q -= e.second; if (q <= 0) return e.first; }
    return ok.empty() ? "cod" : ok[0].first;
  };
  if (r < 0.04f) return {pick({{"saddle", 2}, {"bow", 2}, {"emerald", 3}, {"gold_ingot", 3}, {"diamond", 1}, {"music_disc_tide", 1}, {"music_disc_grove", 1}, {"fishing_rod", 2}}), "treasure", 0};
  if (r < 0.12f) return {pick({{"stick", 3}, {"string", 3}, {"bowl", 2}, {"leather", 2}, {"bone", 3}, {"lily_pad", 2}, {"kelp", 3}, {"ink_sac", 1}}), "junk", 0};
  std::vector<std::pair<const char*, float>> fish =
    ocean ? (temp > 0.3f ? std::vector<std::pair<const char*, float>>{{"tropical_fish", 4}, {"pufferfish", 2}, {"cod", 4}}
             : temp < -0.1f ? std::vector<std::pair<const char*, float>>{{"salmon", 6}, {"cod", 4}}
             : std::vector<std::pair<const char*, float>>{{"cod", 7}, {"salmon", 2}, {"pufferfish", 1}})
          : temp > 0.4f ? std::vector<std::pair<const char*, float>>{{"tropical_fish", 3}, {"salmon", 4}, {"cod", 2}}
          : std::vector<std::pair<const char*, float>>{{"salmon", 6}, {"cod", 3}};
  std::string id = pick(fish);
  return {id, "fish", sizeOf(id)};
}

// ---- the float
float Bobber::surface(Level& lv) const {
  int x = ifloor(pos.x), z = ifloor(pos.z);
  for (int y = ifloor(pos.y) + 1; y >= ifloor(pos.y) - 2; y--)
    if (water(lv, realm, x, y, z) && !water(lv, realm, x, y + 1, z)) return y + 0.88f;
  return NAN;
}

void Bobber::comeUp(Level& lv, float top) {
  FishCatch c = fishingCatch(lv, realm, pos);
  catchId = c.id; catchKind = c.kind; catchCm = c.cm;
  comeT = std::max(0.5f, waitT);
  float a = frand() * TAU, r = 2.6f + frand() * 1.2f;
  vec3 start(pos.x + std::cos(a) * r, top - 1.3f, pos.z + std::sin(a) * r);
  if (!water(lv, realm, ifloor(start.x), ifloor(start.y), ifloor(start.z))) start = vec3(pos.x + std::cos(a) * 1.4f, top - 0.8f, pos.z + std::sin(a) * 1.4f);
  coming = true; comeFrom = start;
  if (catchKind != "fish") return;
  Entity* f = nullptr;
  if (!lv.isRemote()) {   // one already swimming close by: it's what you'll catch
    float best = 10;
    for (auto& e : lv.entities()) {
      if (e->removed || e->ghost || e->realm != realm || !isFish(e->type) || e->hp <= 0 || e->data.contains("hooked") || !e->inWater) continue;
      float d = std::hypot(e->pos.x - pos.x, e->pos.z - pos.z);
      if (d < best && std::fabs(e->pos.y - top) < 6) { best = d; f = e.get(); }
    }
  }
  if (f) { catchId = f->type; comeFrom = f->pos; catchCm = sizeOf(f->type); fishLocal = false; }
  else if (reg().mobs.is_object() && reg().mobs.contains(catchId)) {   // otherwise one comes up from deeper water
    auto e = createEntity(catchId);
    e->type = catchId; e->realm = realm; e->pos = start; e->inWater = true;
    const json& T = reg().mobs[catchId];
    e->w = T.value("w", 0.25f); e->h = T.value("h", 0.3f); e->hp = e->maxHp = T.value("hp", 3.f);
    if (lv.isRemote()) e->netLocal = true;
    f = lv.spawn(std::move(e));
    fishLocal = lv.isRemote();
  }
  if (f) { f->data["hooked"] = id; fish = f->id; }
}

void Bobber::letGo(Level& lv, bool scared) {
  Entity* f = fish ? lv.entity(fish) : nullptr;
  fish = 0; coming = false;
  if (!f) return;
  f->data.erase("hooked");
  if (fishLocal) { f->removed = true; return; }
  if (scared) { f->data["swimDir"] = heading(pos.x, pos.z, f->pos.x, f->pos.z); f->data["swimT"] = 3; f->data["swimY"] = f->pos.y - 1; }
}

void Bobber::tick(Level& lv, double dtd) {
  float dt = (float)dtd;
  Player* p = dynamic_cast<Player*>(lv.entity(owner));
  auto it = floats().find(owner);
  if (!p || p->dead || it == floats().end() || it->second != id) { letGo(lv, false); removed = true; return; }
  if (!isRod(p->held())) { endFishing(lv, *p, false); return; }
  if (glm::distance(pos, p->pos) > 34 || p->realm != realm) { endFishing(lv, *p, false); toast(lv, *p, "The line snapped", "warn"); return; }
  World& w = lv.world(realm);
  if (state == Fly) {
    for (int s = 0; s < 3; s++) {
      float h = dt / 3;
      vel.y -= 16 * h; float k = std::max(0.f, 1 - h * 0.4f); vel.x *= k; vel.z *= k;
      vec3 n = pos + vel * h;
      BlockId b = w.getBlock(ifloor(n.x), ifloor(n.y), ifloor(n.z));
      if (reg().tLiquid[b] == 1) {
        pos = n; state = Float; vel = vec3(0);
        waitT = 5 + frand() * 14;
        sfxAt(lv, "splash_small", pos, 20);
        float top = surface(lv);
        lv.fx->particles("water", vec3(pos.x, (std::isnan(top) ? pos.y : top) + 0.05f, pos.z), 8, {{"spread", 1.5}});
        return;
      }
      if (reg().tSolid[b] || reg().tLiquid[b] == 2) {
        state = Ground; vel = vec3(0);
        if (reg().tLiquid[b] == 2) endFishing(lv, *p, false);
        return;
      }
      pos = n;
    }
    if (pos.y < -10) endFishing(lv, *p, false);
    return;
  }
  if (state != Float) return;
  float top = surface(lv);
  if (std::isnan(top)) { state = Fly; return; }
  // a fish swims up to the float, bubbles trailing off it, noses at it, then bites
  waitT -= dt;
  Entity* f = fish ? lv.entity(fish) : nullptr;
  if (f && f->removed) f = nullptr;
  if (biteT <= 0 && waitT < 3.2f && waitT > 0) {
    if (!coming) { comeUp(lv, top); f = fish ? lv.entity(fish) : nullptr; }
    float t = clampv(1 - waitT / std::min(3.2f, comeT > 0 ? comeT : 3.2f), 0.f, 1.f), e = t * t * (3 - 2 * t);
    vec3 c = comeFrom;
    float bx = c.x + (pos.x - c.x) * e, bz = c.z + (pos.z - c.z) * e, by = c.y + (top - 0.42f - c.y) * e;
    if (f) {
      float side = std::sin(age * 3.1f) * 0.25f * (1 - e);
      float hx = pos.x - c.x, hz = pos.z - c.z, hl = std::hypot(hx, hz); if (hl == 0) hl = 1;
      f->pos = vec3(bx - hz / hl * side, by, bz + hx / hl * side); f->vel = vec3(0);
      f->yaw = heading(f->pos.x, f->pos.z, pos.x, pos.z);
      f->data["speedNow"] = 1.4f * (1 - e) + 0.4f;
      if (frand() < dt * 9) lv.fx->particles("splash", f->pos + vec3(frand2() * 0.1f, 0.15f, frand2() * 0.1f), 1, {{"vel", {0, 0.9, 0}}, {"life", std::max(0.2f, (top - f->pos.y) / 0.9f)}, {"size", 0.045}, {"grav", 0}});
    } else if (frand() < dt * 14) lv.fx->particles("splash", vec3(bx + frand2() * 0.15f, top - 0.02f, bz + frand2() * 0.15f), 1, {{"vel", {0, 0.25, 0}}, {"life", 0.5}, {"size", 0.05}, {"grav", 0}});
  }
  if (biteT <= 0 && waitT <= 0) {
    if (!coming) { comeUp(lv, top); f = fish ? lv.entity(fish) : nullptr; }
    biteT = 0.85f + frand() * 0.3f;
    sfxAt(lv, "splash_small", vec3(pos.x, top, pos.z), 24);
    lv.fx->particles("water", vec3(pos.x, top + 0.05f, pos.z), 10, {{"spread", 2}});
    lv.fx->shake(owner, 0.4f);   // a rumble on a controller
  }
  if (biteT > 0) {
    if (f) { f->pos = vec3(pos.x + std::sin(age * 20) * 0.04f, top - 0.38f + std::sin(age * 30) * 0.03f, pos.z); f->data["speedNow"] = 3; }
    biteT -= dt;
    if (biteT <= 0) { waitT = 4 + frand() * 12; letGo(lv, true); }   // it got away
  }
  float dip = biteT > 0 ? 0.22f + std::sin(age * 30) * 0.03f : 0;
  pos.y += ((top - 0.06f - dip + std::sin(age * 2.6f) * 0.02f) - pos.y) * std::min(1.f, dt * 10);
}

// ---- the angler
Bobber* fishingOf(Level& lv, const Player& p) {
  auto it = floats().find(p.id);
  if (it == floats().end()) return nullptr;
  auto* b = dynamic_cast<Bobber*>(lv.entity(it->second));
  return b && !b->removed ? b : nullptr;
}
float castWindOf(const Player& p) { auto it = winds().find(p.id); return it != winds().end() && it->second > 0 ? 1 - it->second / 0.3f : 0; }
std::string heldRodModel(Level& lv, const Player& p) { return fishingOf(lv, p) ? "fishing_rod_cast" : "fishing_rod"; }

static void castLine(Level& lv, Player& p) {
  vec3 l = lookVec(p), r(std::cos(p.yaw), 0, -std::sin(p.yaw));
  auto b = std::make_unique<Bobber>();
  b->owner = p.id; b->realm = p.realm;
  b->pos = p.pos + vec3(0, EYE + 0.1f, 0) + l * 0.8f + r * 0.3f;
  b->vel = vec3(l.x * 13, l.y * 13 + 3.5f, l.z * 13);
  Entity* e = lv.spawn(std::move(b));
  floats()[p.id] = e->id;
  sfxUi(lv, p, "bow");
}

void endFishing(Level& lv, Player& p, bool reel) {
  Bobber* b = fishingOf(lv, p);
  floats().erase(p.id);
  if (!b) return;
  b->removed = true;
  bool bite = b->state == Bobber::Float && b->biteT > 0;
  if (!(bite && reel)) b->letGo(lv, true);
  if (!reel) return;
  if (b->state == Bobber::Float) sfxAt(lv, "splash_small", b->pos, 16);
  if (bite) {
    FishCatch c{b->catchId, b->catchKind, b->catchCm};
    if (c.id.empty()) c = fishingCatch(lv, b->realm, b->pos);
    if (Entity* f = b->fish ? lv.entity(b->fish) : nullptr) { f->data.erase("hooked"); f->removed = true; }
    b->fish = 0;
    // up out of the water and over to you
    vec3 d = p.pos + vec3(0, 1.2f, 0) - b->pos; float t = 0.9f;
    dropItem(lv, b->realm, ItemStack(c.id, 1), b->pos + vec3(0, 0.3f, 0), vec3(d.x / t, d.y / t + 0.5f * 18 * t, d.z / t));
    grantXp(lv, p, c.kind == "treasure" ? 6 : 1 + (int)(frand() * 3), b->pos + vec3(0, 0.5f, 0));
    const ItemDef* def = reg().item(c.id);
    std::string name = def ? def->name : c.id;
    toast(lv, p, c.kind == "fish" ? "You caught a " + toLower(name) + " (" + std::to_string(c.cm) + " cm)!" : c.kind == "treasure" ? "Treasure! " + name : "Reeled in some " + toLower(name),
          c.kind == "treasure" ? "good" : "");
    sfxUi(lv, p, c.kind == "treasure" ? "levelup" : "pop");
    if (lv.events) lv.events->emit("fishCaught", json::array({p.name, c.id, c.kind, c.cm}));
    wearHeld(lv, p, 1);
  } else if (b->state == Bobber::Float) wearHeld(lv, p, 1);
}

bool fishingUse(UseCtx& u) {
  Player& p = u.p;
  if (!isRod(p.held()) || u.item != p.held().id) return false;
  if (fishingOf(u.lv, p)) endFishing(u.lv, p, true);
  else if (!(winds()[p.id] > 0)) winds()[p.id] = 0.3f;
  return true;
}

void fishingTick(Level& lv, double dt) {
  for (Player* p : levelPlayers(lv)) {
    auto it = winds().find(p->id);
    if (it != winds().end() && it->second > 0) {
      it->second -= (float)dt;
      if (p->held().empty() || p->held().id != "fishing_rod") it->second = 0;
      else if (it->second <= 0) { it->second = 0; castLine(lv, *p); }
    }
    if (p->dead && fishingOf(lv, *p)) endFishing(lv, *p, false);
    bool out = fishingOf(lv, *p) != nullptr;   // (the third-person hand draws the rod without its hanging line while the float is out)
    if (p->data.value("rodOut", false) != out) p->data["rodOut"] = out;
  }
}

void registerFishingEntities() {
  registerEntityFactory("fishing_bobber", [](const std::string&) { return std::make_unique<Bobber>(); });
}

} // namespace dq
