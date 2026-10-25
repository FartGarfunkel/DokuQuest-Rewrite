// sim/content.cpp — the content features' shared helpers and the ContentSystem that drives them.
#include "sim/content.h"
#include "sim/maps.h"
#include <random>

namespace dq {

ContentHooks& contentHooks() { static ContentHooks h; return h; }

// ------------------------------------------------------------------ helpers
std::vector<Player*> levelPlayers(Level& lv, bool withGhosts) {
  std::vector<Player*> out;
  for (auto& e : lv.entities()) if (e && !e->removed && e->isPlayer() && (withGhosts || !e->ghost)) if (auto p = dynamic_cast<Player*>(e.get())) out.push_back(p);
  return out;
}
Player* findPlayer(Level& lv, const std::string& name) {
  for (Player* p : levelPlayers(lv)) if (p->name == name) return p;
  return nullptr;
}
float frand() {
  static thread_local std::mt19937 rng{std::random_device{}()};
  return std::uniform_real_distribution<float>(0.f, 1.f)(rng);
}
vec3 lookVec(const Player& p) { return vec3(-std::sin(p.yaw) * std::cos(p.pitch), std::sin(p.pitch), -std::cos(p.yaw) * std::cos(p.pitch)); }

void hurtPlayer(Level& lv, Player& p, float amount, const std::string& type) {
  if (amount <= 0 || p.dead) return;
  if (type == "fall" && countItem(p, "golden_feather") > 0) {   // places.js: the golden feather makes every fall gentle
    lv.fx->particles("white", p.pos + vec3(0, 0.2f, 0), 8, {{"spread", 0.6}});
    return;
  }
  if (auto& h = contentHooks().hurtPlayer) { h(lv, p, amount, type); return; }
  p.hp = std::max(0.f, p.hp - amount); p.hurtTime = 0.5f;
}
void grantXp(Level& lv, Player& p, int xp, vec3 at) {
  if (auto& h = contentHooks().grantXp) { h(lv, p, xp, at); return; }
  p.xp += xp;
}
Entity* dropItem(Level& lv, int realm, const ItemStack& s, vec3 pos, vec3 vel) {
  if (s.empty()) return nullptr;
  if (auto& h = contentHooks().dropItem) return h(lv, realm, s, pos, vel);
  auto e = createEntity("item");
  e->type = "item"; e->realm = realm; e->pos = pos; e->vel = vel; e->w = 0.125f; e->h = 0.25f;
  e->data["stack"] = s.toJson();
  return lv.spawn(std::move(e));
}
void giveOrDrop(Level& lv, Player& p, const ItemStack& s) {
  int left = p.give(s);
  if (left > 0) { ItemStack r = s; r.n = left; dropItem(lv, p.realm, r, p.pos + vec3(0, 1.2f, 0)); }
}
void useUpHeld(Player& p, int n) {
  if (creativeOf(p)) return;
  ItemStack& h = p.held();
  h.n -= n; if (h.n <= 0) h.clear();
}
int countItem(const Player& p, const std::string& id) {
  int n = 0;
  for (auto& s : p.inv) if (!s.empty() && s.id == id) n += s.n;
  for (auto& s : p.offhand) if (!s.empty() && s.id == id) n += s.n;
  return n;
}
int findSafeY(World& w, int x, int z, int dim, int top) {
  const Registry& R = reg();
  int base = dimBase(dim), hi = top >= 0 ? std::min(top, base + DIM_H - 2) : base + DIM_H - 2;
  for (int y = hi; y > base; y--) {
    BlockId b = w.getBlock(x, y - 1, z);
    if (!R.tSolid[b] || R.tLiquid[b]) continue;
    BlockId a = w.getBlock(x, y, z), c = w.getBlock(x, y + 1, z);
    if ((a == AIR || !R.tSolid[a]) && !R.tLiquid[a] && (c == AIR || !R.tSolid[c]) && !R.tLiquid[c]) return y;
  }
  return -1;
}
std::string jsDimName(int d) {
  static const char* N[] = {"Overworld", "Ember Depths", "Skylands", "Pocket Dimension", "His Realm", "The End"};
  return d >= 0 && d < 6 ? N[d] : "Somewhere";
}
int jsDimOf(int realm, int y) {
  if (realm == REALM_POCKET) return JS_POCKET;
  if (realm == REALM_HIS) return JS_REALM;
  return jsFromDim(dimOfY(y));
}
int jsDimOf(const Entity& e) { return jsDimOf(e.realm, ifloor(e.pos.y)); }
std::string capFirst(std::string s) { if (!s.empty()) s[0] = (char)toupper((unsigned char)s[0]); return s; }
std::string newId(const char* prefix) {
  static const char* D = "0123456789abcdefghijklmnopqrstuvwxyz";
  std::string t; u64 ms = (u64)unixMillis();
  while (ms) { t.insert(t.begin(), D[ms % 36]); ms /= 36; }
  if (t.size() > 6) t = t.substr(t.size() - 6);
  std::string r = prefix;
  r += t;
  for (int i = 0; i < 4; i++) r += D[(int)(frand() * 36) % 36];
  return r;
}
double playTime(Level& lv) {
  if (!lv.info.data.is_object()) lv.info.data = json::object();
  auto it = lv.info.data.find("playTime");
  return it != lv.info.data.end() && it->is_number() ? it->get<double>() : 0.0;
}

void openContentScreen(Level& lv, const Player& p, const std::string& id, const json& args) {
  if (lv.events) lv.events->emit("contentScreen", json::array({p.name, id, args}));
}
void contentAction(Level& lv, Player& p, const std::string& kind, const json& args) {
  json a = args.is_object() ? args : json::object();
  a["k"] = kind;
  if (lv.isRemote() && lv.net) { lv.net->action(p, "content", a); return; }
  if (kind == "stable") stableAction(lv, p, a);
}

// ------------------------------------------------------------------ the system
namespace {
class ContentSystem : public LevelSystem {
public:
  explicit ContentSystem(Level& l) : lv(l) {
    if (!lv.events) return;
    // itemUse [playerName, itemKey, x, y, z, face] — before the default action; false = used up
    hUse = lv.events->on("itemUse", [this](const json& a) {
      if (!a.is_array() || a.size() < 6) return true;
      Player* p = findPlayer(lv, a[0].is_string() ? a[0].get<std::string>() : "");
      if (!p || p->dead) return true;
      UseCtx u{lv, *p, a[1].is_string() ? a[1].get<std::string>() : "", ivec3(a[2].get<int>(), a[3].get<int>(), a[4].get<int>()), a[5].get<int>()};
      bool used = stableUse(u) || fishingUse(u) || elytraUse(u) || farmingUse(u) || campingUse(u) || travelUse(u) || mapsUse(u);
      return !used;
    });
    // blockBreak [player, x, y, z, key] — before the block goes
    hBreak = lv.events->on("blockBreak", [this](const json& a) {
      if (!a.is_array() || a.size() < 5) return true;
      Player* p = findPlayer(lv, a[0].is_string() ? a[0].get<std::string>() : "");
      if (!p) return true;
      World& w = lv.world(p->realm);
      int x = a[1], y = a[2], z = a[3];
      return campingBreak(lv, *p, x, y, z, w.getBlock(x, y, z), w.getMeta(x, y, z));
    });
    // netAction [player, name, args] — the host does what a client asked
    hAct = lv.events->on("netAction", [this](const json& a) {
      if (!a.is_array() || a.size() < 3 || a[1] != "content") return true;
      Player* p = findPlayer(lv, a[0].is_string() ? a[0].get<std::string>() : "");
      if (!p) return true;
      const json& args = a[2];
      if (args.is_object() && args.value("k", "") == "stable") stableAction(lv, *p, args);
      return false;
    });
  }
  ~ContentSystem() override { if (lv.events) { lv.events->off(hUse); lv.events->off(hBreak); lv.events->off(hAct); } }
  const char* name() const override { return "content"; }
  void tick(Level& l, double dt) override {
    if (!l.isRemote()) l.info.data["playTime"] = playTime(l) + dt;
    mapsTick(l, dt);
    fishingTick(l, dt);
    campingTick(l, dt);
    travelTick(l, dt);
    elytraTick(l, dt);
    stableTick(l, dt);
    placesTick(l, dt);
  }
  void save(Level& l) override { mapsSave(l); }
private:
  Level& lv;
  int hUse = 0, hBreak = 0, hAct = 0;
};
}

void registerFishingEntities();
void registerCampingEntities();

void registerContent() {
  static bool done = false;
  if (done) return;
  done = true;
  registerFishingEntities();
  registerCampingEntities();
  registerLevelSystem([](Level& lv) { return std::make_shared<MapStore>(lv); });
  registerLevelSystem([](Level& lv) { return std::make_shared<ContentSystem>(lv); });
}

} // namespace dq
