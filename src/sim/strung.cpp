// sim/strung.cpp — see strung.h.
#include "sim/strung.h"
#include "common/common.h"
#include "sim/player.h"
#include "sim/events.h"
#include "sim/gameplay.h"
#include "sim/physics.h"
#include "sim/moving.h"
#include "sim/redstone.h"
#include "core/registry.h"
#include <algorithm>
#include <cmath>

namespace dq {

constexpr float STRUNG_MAX = 16.f;

namespace {
json& rec(Level& lv) { json& S = lv.info.data["strung"]; if (!S.is_array()) S = json::array(); return S; }
ivec3 jv(const json& a) { return a.is_array() && a.size() >= 3 ? ivec3(a[0].get<int>(), a[1].get<int>(), a[2].get<int>()) : ivec3(0); }
json vj(ivec3 p) { return json::array({p.x, p.y, p.z}); }
Strung fromJson(const json& r) { Strung s; s.id = r.value("id", 0u); s.realm = r.value("r", 0); s.a = jv(r["a"]); s.b = jv(r["b"]); s.chain = r.value("k", "") == "chain"; s.slats = r.value("s", false); return s; }
json toJson(const Strung& s) { return json{{"id", s.id}, {"r", s.realm}, {"a", vj(s.a)}, {"b", vj(s.b)}, {"k", s.chain ? "chain" : "rope"}, {"s", s.slats}}; }

// a block's surface toward the other end (along the way it mostly runs)
vec3 faceToward(ivec3 c, ivec3 other) {
  const vec3 d = vec3(other - c); vec3 o(0);
  const vec3 ad = glm::abs(d);
  if (ad.x >= ad.y && ad.x >= ad.z) o.x = d.x > 0 ? 0.5f : -0.5f; else if (ad.y >= ad.z) o.y = d.y > 0 ? 0.5f : -0.5f; else o.z = d.z > 0 ? 0.5f : -0.5f;
  return vec3(c) + vec3(0.5f) + o;
}
// the point of a block cell now: where a moving piece has it, if it's lifted out
vec3 cellNow(Level& lv, int realm, ivec3 c, vec3 p) {
  for (const Body& b : movingBodiesList(lv)) {
    if (b.realm != realm) continue;
    for (const BodyBlock& k : b.blocks) if (b.m.machine + k.rel == c) return vec3(glm::translate(mat4(1), vec3(b.m.machine)) * b.transform() * vec4(p - vec3(b.m.machine), 1));
  }
  return p;
}
bool inBody(Level& lv, int realm, ivec3 c) {
  for (const Body& b : movingBodiesList(lv)) if (b.realm == realm) for (const BodyBlock& k : b.blocks) if (b.m.machine + k.rel == c) return true;
  return false;
}
int itemsFor(float len) { return std::max(1, (int)std::ceil(len / 2.f)); }
bool isSlatItem(const std::string& id) { return id.find("planks") != std::string::npos || id.find("_slab") != std::string::npos; }
}

float strungSag(const Strung& s, float len) { return len * (s.slats ? 0.025f : s.chain ? 0.09f : 0.06f); }

std::vector<Strung> strungList(Level& lv) {
  std::vector<Strung> out;
  for (auto& r : rec(lv)) if (r.is_object()) out.push_back(fromJson(r));
  return out;
}
vec3 strungEnd(Level& lv, const Strung& s, bool second) {
  const ivec3 c = second ? s.b : s.a, o = second ? s.a : s.b;
  vec3 p = faceToward(c, o);
  // a thin block (a fence, a wall, a post, a lantern hook): tied round its middle, not to the face of its cell
  if (World* w = lv.worldIfOpen(s.realm); w && w->isLoaded(c.x, c.y, c.z)) {
    const BlockId id = w->getBlock(c.x, c.y, c.z);
    if (id && !reg().tOpaque[id]) p = vec3(c) + vec3(0.5f, 0.75f, 0.5f);
  }
  return cellNow(lv, s.realm, c, p);
}
vec3 strungPoint(Level& lv, const Strung& s, float t) {
  const vec3 a = strungEnd(lv, s, false), b = strungEnd(lv, s, true);
  const float len = glm::distance(a, b);
  const float horiz = std::hypot(b.x - a.x, b.z - a.z);
  const float k = len > 0.01f ? horiz / len : 0.f;   // (a straight-down one doesn't sag sideways)
  return glm::mix(a, b, t) - vec3(0, strungSag(s, len) * k * 4 * t * (1 - t), 0);
}

void strungAdd(Level& lv, int realm, ivec3 a, ivec3 b, bool chain) {
  Strung s; s.realm = realm; s.a = a; s.b = b; s.chain = chain;
  u32 id = lv.info.data.value("strungNext", 1u); lv.info.data["strungNext"] = id + 1; s.id = id;
  rec(lv).push_back(toJson(s));
  if (lv.events) lv.events->emit("netBroadcast", json::array({"strungSync", rec(lv)}));
}

namespace {
class StrungSystem : public LevelSystem {
public:
  explicit StrungSystem(Level& l) : lv(l) {
    all().push_back(this);
    if (!lv.events) return;
    hUse = lv.events->on("itemUse", [this](const json& a) { return use(a); });
    hAct = lv.events->on("netAction", [this](const json& a) {
      if (lv.isRemote() || !a.is_array() || a.size() < 3 || a[1] != "strungUse" || !a[2].is_array()) return true;
      use(a[2]); return false;
    });
    hSync = lv.events->on("strungSync", [this](const json& a) { if (lv.isRemote() && a.is_array()) { lv.info.data["strung"] = a; rebuild(); } return true; });
    hMoved = lv.events->on("movingMoved", [this](const json& a) { moved(a); return true; });
  }
  ~StrungSystem() override {
    auto& v = all(); v.erase(std::remove(v.begin(), v.end(), this), v.end());
    if (lv.events) { lv.events->off(hUse); lv.events->off(hAct); lv.events->off(hSync); lv.events->off(hMoved); }
  }
  const char* name() const override { return "strung"; }
  static std::vector<StrungSystem*>& all() { static std::vector<StrungSystem*> v; return v; }

  void fresh() { if (dirty) rebuild(); }
  void tick(Level&, double dt) override {
    if (!movingBodiesList(lv).empty() && !rec(lv).empty()) dirty = true;   // (an end may be riding a moving piece)
    if (rec(lv).size() != lastN) { lastN = rec(lv).size(); dirty = true; }   // (an end may be riding a moving piece)
    if (dirty) rebuild();
    if (lv.isRemote()) return;
    if ((checkT += dt) < 1.0) return;
    checkT = 0;
    // an end's block gone (not just riding a moving piece): it comes down, its chain or rope (and slats) dropped
    json& R = rec(lv); bool changed = false;
    for (size_t i = 0; i < R.size();) {
      const Strung s = fromJson(R[i]);
      World* w = lv.worldIfOpen(s.realm);
      bool gone = false;
      if (w) for (const ivec3& c : {s.a, s.b}) {
        if (!w->isLoaded(c.x, c.y, c.z) || inBody(lv, s.realm, c)) continue;
        const BlockId id = w->getBlock(c.x, c.y, c.z);
        if (!id || reg().tReplaceable[id] || reg().tLiquid[id]) gone = true;
      }
      if (!gone) { i++; continue; }
      DQ_LOG("strung: %u comes down (an end's block is gone)", s.id);
      const vec3 a = strungEnd(lv, s, false), b = strungEnd(lv, s, true), mid = (a + b) * 0.5f;
      const int n = itemsFor(glm::distance(a, b));
      dropItem(lv, s.realm, ItemStack(s.chain ? "chain" : "rope", n), mid, true);
      if (s.slats) dropItem(lv, s.realm, ItemStack(R[i].value("slat", std::string("planks")), n), mid, true);
      R.erase(R.begin() + i); changed = true;
    }
    if (changed) { dirty = true; sync(); }
    if ((syncT += 1.0) >= 5.0) { syncT = 0; sync(); }   // (and now and then, for anyone who joined since)
  }

  // physics: rope bridges' slats
  static void extraBoxes(const World& w, double x0, double y0, double z0, double x1, double y1, double z1, std::vector<DBox>& out) {
    for (StrungSystem* s : all()) for (auto& [realm, list] : (s->fresh(), s->boxes)) {
      if (s->lv.worldIfOpen(realm) != &w) continue;
      for (auto& q : list) if (q.x1 > x0 && q.x0 < x1 && q.y1 > y0 && q.y0 < y1 && q.z1 > z0 && q.z0 < z1) out.push_back(q);
    }
  }
  // climbing: a steep strung one within reach of you
  static bool extraClimb(const World& w, const Entity& e) {
    for (StrungSystem* s : all()) for (auto& [realm, lines] : (s->fresh(), s->steep)) {
      if (s->lv.worldIfOpen(realm) != &w) continue;
      for (auto& pts : lines) for (const vec3& p : pts)
        if (p.y >= e.pos.y - 0.1f && p.y <= e.pos.y + e.h && std::hypot(p.x - e.pos.x, p.z - e.pos.z) < e.w + 0.35f) return true;
    }
    return false;
  }

private:
  Level& lv; int hUse = -1, hAct = -1, hSync = -1, hMoved = -1;
  double checkT = 0, syncT = 0; bool dirty = true; size_t lastN = 0;
  std::map<int, std::vector<DBox>> boxes;
  std::map<int, std::vector<std::vector<vec3>>> steep;

  void sync() {
    dirty = true; if (lv.events && !lv.isRemote()) lv.events->emit("netBroadcast", json::array({"strungSync", rec(lv)})); }

  void rebuild() {
    dirty = false; boxes.clear(); steep.clear();
    for (const Strung& s : strungList(lv)) {
      const vec3 a = strungEnd(lv, s, false), b = strungEnd(lv, s, true);
      const float len = glm::distance(a, b), horiz = std::hypot(b.x - a.x, b.z - a.z);
      const int n = std::max(2, (int)std::ceil(len * 2));
      if (s.slats) {
        const vec2 d = horiz > 0.01f ? vec2(b.x - a.x, b.z - a.z) / horiz : vec2(1, 0);
        const float hx = 0.3f * std::fabs(d.x) + 0.55f * std::fabs(d.y), hz = 0.3f * std::fabs(d.y) + 0.55f * std::fabs(d.x);
        for (int i = 0; i <= n; i++) { const vec3 p = strungPoint(lv, s, (float)i / n); boxes[s.realm].push_back({p.x - hx, p.y - 0.12, p.z - hz, p.x + hx, (double)p.y, p.z + hz}); }
      } else if (std::fabs(b.y - a.y) > horiz * 1.7f) {
        std::vector<vec3> pts; for (int i = 0; i <= n; i++) pts.push_back(strungPoint(lv, s, (float)i / n));
        steep[s.realm].push_back(std::move(pts));
      }
    }
  }

  // a piece set down: its strung ends go where its blocks went ({"realm", "moves": [[fx,fy,fz,tx,ty,tz]...]})
  void moved(const json& a) {
    if (lv.isRemote() || !a.is_object() || !a.contains("moves")) return;
    const int realm = a.value("realm", 0);
    std::map<std::array<int, 3>, ivec3> to;
    for (auto& m : a["moves"]) if (m.is_array() && m.size() >= 6) to[{m[0].get<int>(), m[1].get<int>(), m[2].get<int>()}] = ivec3(m[3].get<int>(), m[4].get<int>(), m[5].get<int>());
    bool changed = false;
    for (auto& r : rec(lv)) {
      if (r.value("r", 0) != realm) continue;
      for (const char* k : {"a", "b"}) { const ivec3 c = jv(r[k]); auto it = to.find({c.x, c.y, c.z}); if (it != to.end()) { r[k] = vj(it->second); changed = true; } }
    }
    if (changed) { dirty = true; sync(); }
  }

  bool use(const json& a) {
    if (!a.is_array() || a.size() < 6 || !a[1].is_string()) return true;
    const std::string item = a[1].get<std::string>();
    const bool line = item == "chain" || item == "rope", slat = isSlatItem(item);
    if (!line && !slat) return true;
    Player* p = rs::playerNamed(lv, a[0].is_string() ? a[0].get<std::string>() : "");
    if (!p || p->held().id != item) return true;
    if (line && !p->sneaking) return true;   // (a plain use hangs a chain or rope block as always)
    const bool hasHit = a[5].is_number() && a[5].get<int>() >= 0;
    if (line && !hasHit) return true;
    if (slat && strungList(lv).empty()) return true;
    if (lv.isRemote()) { if (slat && !aimedRope(*p)) return true; if (lv.net) lv.net->action(*p, "strungUse", a); return false; }
    return line ? stringEnd(*p, item, ivec3(a[2].get<int>(), a[3].get<int>(), a[4].get<int>())) : laySlats(*p, item);
  }

  bool take(Player& p, const std::string& id, int n) {
    if (p.mode == "creative") return true;
    int have = 0; for (auto& q : p.inv) if (q.id == id) have += q.n;
    if (have < n) return false;
    for (int i = (int)p.inv.size() - 1; i >= 0 && n > 0; i--) { ItemStack& q = p.inv[i]; if (q.id != id) continue; const int k = std::min(n, q.n); q.n -= k; n -= k; if (q.n <= 0) q.clear(); }
    return true;
  }

  bool stringEnd(Player& p, const std::string& kind, ivec3 at) {
    json& F = p.data["strungFrom"];
    if (!F.is_object() || F.value("k", "") != kind || F.value("r", -1) != p.realm) {
      F = json{{"k", kind}, {"r", p.realm}, {"at", vj(at)}};
      toast(lv, p, "Now sneak-use it on where the other end goes (up to 16 blocks)");
      return false;
    }
    const ivec3 from = jv(F["at"]);
    p.data.erase("strungFrom");
    if (from == at) { toast(lv, p, "Let go"); return false; }
    if (glm::distance(vec3(from), vec3(at)) > STRUNG_MAX) { toast(lv, p, "Too far: up to 16 blocks"); return false; }
    Strung s; s.realm = p.realm; s.a = from; s.b = at; s.chain = kind == "chain";
    const float len = glm::distance(faceToward(from, at), faceToward(at, from));
    const int n = itemsFor(len);
    if (!take(p, kind, n)) { toast(lv, p, "That needs " + std::to_string(n) + " " + (s.chain ? "chains" : "rope")); return false; }
    u32 id = lv.info.data.value("strungNext", 1u); lv.info.data["strungNext"] = id + 1; s.id = id;
    rec(lv).push_back(toJson(s));
    DQ_LOG("strung: %s %u from %d %d %d to %d %d %d", kind.c_str(), s.id, from.x, from.y, from.z, at.x, at.y, at.z);
    sfxAt(lv, s.chain ? "chain_place" : "wool_place", vec3(at) + vec3(0.5f));
    dirty = true; sync();
    return false;
  }

  // the near-level rope you're looking at (within 1 block of your line of sight, 6 blocks off), or -1
  int aimedRope(Player& p) {
    const vec3 eye = p.pos + vec3(0, p.eye > 0 ? p.eye : 1.62f, 0), dir = lookVector(p.yaw, p.pitch);
    const auto L = strungList(lv);
    int best = -1; float bd = 1.0f;
    for (size_t i = 0; i < L.size(); i++) {
      const Strung& s = L[i];
      if (s.chain || s.realm != p.realm) continue;
      const vec3 a = strungEnd(lv, s, false), b = strungEnd(lv, s, true);
      if (std::fabs(b.y - a.y) > 0.3f * std::hypot(b.x - a.x, b.z - a.z) + 0.5f) continue;   // (only a near-level one)
      for (int k = 0; k <= 24; k++) {
        const vec3 q = strungPoint(lv, s, k / 24.f), v = q - eye;
        const float t = glm::dot(v, dir);
        if (t < 0 || t > 6) continue;
        const float d = glm::length(v - dir * t);
        if (d < bd) { bd = d; best = (int)i; }
      }
    }
    return best;
  }

  bool laySlats(Player& p, const std::string& item) {
    const int i = aimedRope(p);
    if (i < 0) return true;
    json& r = rec(lv)[i];
    if (r.value("s", false)) { toast(lv, p, "It already has slats"); return false; }
    const Strung s = fromJson(r);
    const int n = itemsFor(glm::distance(strungEnd(lv, s, false), strungEnd(lv, s, true)));
    if (!take(p, item, n)) { toast(lv, p, "That needs " + std::to_string(n) + " planks or slabs"); return false; }
    r["s"] = true; r["slat"] = item;
    sfxAt(lv, "wood_place", (strungEnd(lv, s, false) + strungEnd(lv, s, true)) * 0.5f);
    dirty = true; sync();
    return false;
  }
};
}

void registerStrung() {
  setExtraBoxes(&StrungSystem::extraBoxes);
  addExtraClimb(&StrungSystem::extraClimb);
  registerLevelSystem([](Level& lv) { return std::make_shared<StrungSystem>(lv); });
}

} // namespace dq
