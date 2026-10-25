// sim/stable.cpp — the book of companions, summoning, dismissing, resting (game/stable.js).
#include "sim/stable.h"
#include "sim/travel.h"

namespace dq {

namespace {
json& stableBook(Level& lv, const std::string& owner) {
  json& D = lv.info.data;
  if (!D.contains("stable") || !D["stable"].is_object()) D["stable"] = json::object();
  json& S = D["stable"];
  if (!S.contains(owner) || !S[owner].is_array()) S[owner] = json::array();
  return S[owner];
}
json* stableRec(Level& lv, const std::string& owner, const std::string& uid) {
  if (owner.empty() || uid.empty()) return nullptr;
  for (auto& r : stableBook(lv, owner)) if (r.value("uid", "") == uid) return &r;
  return nullptr;
}
const json& mobT(const std::string& type) { static const json none = json::object(); return reg().mobs.is_object() && reg().mobs.contains(type) ? reg().mobs[type] : none; }
std::string label(const std::string& type) {
  std::string l = mobT(type).value("label", "");
  if (l.empty()) { l = type; std::replace(l.begin(), l.end(), '_', ' '); return l; }
  for (const char* a : {"an ", "a ", "the "}) if (startsWith(l, a)) return l.substr(strlen(a));
  return l;
}
std::string stableName(const json& r) { return r.value("name", json()).is_string() ? r["name"].get<std::string>() : label(r.value("type", "")); }
std::string capName(const json& r) { return capFirst(r.value("name", json()).is_string() ? r["name"].get<std::string>() : "your " + stableName(r)); }
std::string sdata(const Entity& e, const char* k) { auto it = e.data.find(k); return it != e.data.end() && it->is_string() ? it->get<std::string>() : ""; }

void stableSync(json& r, const Entity& m) {
  r["type"] = m.type;
  r["name"] = m.data.contains("petName") && m.data["petName"].is_string() ? m.data["petName"] : json();
  r["saddled"] = m.data.value("saddled", false);
  r["variant"] = m.data.value("variant", json());
  r["color"] = m.data.value("color", json());
  r["size"] = m.data.value("size", json());
  r["baby"] = m.data.value("baby", false) ? (int)std::lround(m.data.value("growT", 0.0)) : 0;
  r["hp"] = std::max(1, (int)std::lround(m.hp)); r["maxHp"] = m.maxHp;
  r["dim"] = jsDimOf(m); r["realm"] = m.realm;
  r["x"] = (int)std::lround(m.pos.x); r["y"] = (int)std::lround(m.pos.y); r["z"] = (int)std::lround(m.pos.z);
  if (m.data.contains("hs")) r["hs"] = m.data["hs"];
}
void poof(Level& lv, const Entity& e) {
  vec3 c = e.pos + vec3(0, e.h * 0.5f, 0);
  lv.fx->particles("white", c, 14, {{"spread", 1.2}, {"vel", 2}});
  lv.fx->particles("mana_light", c, 18, {{"spread", 0.8}, {"life", 0.6}, {"size", 0.07}});
}
Player* riderOf(Level& lv, const Entity& m) {
  for (Player* p : levelPlayers(lv, true)) if (p->riding == m.id) return p;
  return nullptr;
}
void dismountAll(Level& lv, Entity& m) { for (Player* p : levelPlayers(lv, true)) if (p->riding == m.id) p->riding = 0; m.data.erase("riddenBy"); }
bool collides(World& w, vec3 lo, vec3 hi) {
  for (int x = ifloor(lo.x); x <= ifloor(hi.x - 1e-3f); x++) for (int y = ifloor(lo.y); y <= ifloor(hi.y - 1e-3f); y++) for (int z = ifloor(lo.z); z <= ifloor(hi.z - 1e-3f); z++)
    if (reg().tSolid[w.getBlock(x, y, z)]) return true;
  return false;
}
// a free place to stand beside someone, for a creature of this size
vec3 stableSpot(Level& lv, const Player& pawn, float w, float h) {
  World& W = lv.world(pawn.realm);
  static const float A[8][2] = {{PI / 2, 2}, {-PI / 2, 2}, {PI, 2.5f}, {PI / 2, 3.5f}, {-PI / 2, 3.5f}, {0, 3}, {PI * 0.75f, 3}, {-PI * 0.75f, 3}};
  for (auto& o : A) {
    float a = pawn.yaw + o[0], x = pawn.pos.x - std::sin(a) * (o[1] + w), z = pawn.pos.z - std::cos(a) * (o[1] + w);
    for (int y = ifloor(pawn.pos.y) + 2; y >= ifloor(pawn.pos.y) - 3; y--) {
      if (!reg().tSolid[W.getBlock(ifloor(x), y - 1, ifloor(z))]) continue;
      if (collides(W, vec3(x - w, (float)y, z - w), vec3(x + w, y + h, z + w))) continue;
      return vec3(x, (float)y, z);
    }
  }
  return pawn.pos;
}
Entity* stableLive(Level& lv, const std::string& uid) {
  for (auto& e : lv.entities()) if (!e->removed && !e->ghost && e->hp > 0 && sdata(*e, "uid") == uid) return e.get();
  return nullptr;
}
std::unordered_map<u32, float>& syncT() { static std::unordered_map<u32, float> m; return m; }
}

bool stableIsMount(const std::string& type) { return mountBase(type).has_value(); }

// keep the book in step with the creature, and see off any copy that's been replaced
static void stableTickOne(Level& lv, Entity& m, double dt) {
  std::string owner = sdata(m, "owner");
  std::string uid = sdata(m, "uid");
  if (uid.empty()) {
    uid = "c" + newId().substr(2, 4) + newId().substr(6, 5);
    m.data["uid"] = uid; m.data["sgen"] = 1;
    json r = {{"uid", uid}, {"gen", 1}, {"state", "out"}, {"joined", (int)std::lround(playTime(lv))}};
    stableSync(r, m);
    stableBook(lv, owner).push_back(r);
    if (Player* o = findPlayer(lv, owner)) toast(lv, *o, capFirst(stableName(r)) + " joins your stable \xC2\xB7 H to call your companions", "good");
    return;
  }
  json* r = stableRec(lv, owner, uid);
  int sg = m.data.value("sgen", 1);
  if (!r) {
    json n = {{"uid", uid}, {"gen", sg}, {"state", "out"}};
    stableSync(n, m);
    stableBook(lv, owner).push_back(n);
    return;
  }
  if ((*r).value("gen", 1) != sg || (*r).value("state", "") != "out") { dismountAll(lv, m); m.removed = true; return; }
  float& t = syncT()[m.id];
  if ((t -= (float)dt) <= 0) { t = 2; stableSync(*r, m); }
}

void stableTick(Level& lv, double dt) {
  if (lv.isRemote()) return;
  for (size_t i = 0; i < lv.entities().size(); i++) {
    Entity& e = *lv.entities()[i];
    if (e.removed || e.ghost || e.isPlayer() || e.hp <= 0 || sdata(e, "owner").empty() || e.data.contains("mountedOn")) continue;
    if (mobT(e.type).value("spirit", false)) continue;
    stableTickOne(lv, e, dt);
  }
}

std::string stableSummon(Level& lv, Player& p, const std::string& uid) {
  json* rp = stableRec(lv, p.name, uid);
  if (!rp) return "You don't have that companion";
  json& r = *rp;
  double now = playTime(lv);
  if (r.value("state", "") == "resting" && now < r.value("restUntil", 0.0))
    return capName(r) + " is still resting (" + std::to_string((int)std::ceil((r.value("restUntil", 0.0) - now) / 60)) + " min)";
  sfxAt(lv, "whistle", p.pos + vec3(0, 1.5f, 0));
  if (Entity* live = stableLive(lv, uid)) {
    // already about: it comes to you
    if (riderOf(lv, *live)) return capName(r) + " is being ridden";
    poof(lv, *live);
    live->realm = p.realm;
    live->pos = stableSpot(lv, p, live->w, live->h); live->prevPos = live->pos; live->vel = vec3(0);
    live->data["stay"] = false; live->data["sitting"] = false;
    poof(lv, *live);
    return "";
  }
  std::string type = r.value("type", "");
  if (!reg().mobs.is_object() || !reg().mobs.contains(type)) return "That creature can't be called here";
  r["gen"] = r.value("gen", 1) + 1; r["state"] = "out";
  const json& T = mobT(type);
  bool baby = r.value("baby", 0) != 0;
  float w = T.value("w", 0.4f) * (baby ? 0.55f : 1.f), h = T.value("h", 1.f) * (baby ? 0.55f : 1.f);
  auto e = createEntity(type);
  e->type = type; e->realm = p.realm; e->pos = stableSpot(lv, p, w, h); e->w = w; e->h = h; e->yaw = p.yaw; e->persistent = true;
  e->maxHp = T.value("hp", 10.f);
  json& D = e->data;
  D["owner"] = p.name; D["tamed"] = true; D["kept"] = true; D["uid"] = uid; D["sgen"] = r["gen"];
  if (r.value("name", json()).is_string()) D["petName"] = r["name"];
  if (r.value("saddled", false)) D["saddled"] = true;
  for (const char* k : {"color", "variant", "size"}) if (r.contains(k) && !r[k].is_null()) D[k] = r[k];
  if (baby) { D["baby"] = true; D["growT"] = r["baby"]; }
  if (r.contains("hs") && r["hs"].is_array()) { D["hs"] = r["hs"]; e->hp = e->maxHp; hsApplyHp(*e); }
  e->hp = std::min(e->maxHp, r.value("hp", e->maxHp) > 0 ? r.value("hp", e->maxHp) : e->maxHp);
  if (T.value("flyingMount", false) && !p.onGround) e->noGravity = true;
  Entity* m = lv.spawn(std::move(e));
  poof(lv, *m);
  return "";
}

std::string stableDismiss(Level& lv, Player& p, const std::string& uid) {
  json* r = stableRec(lv, p.name, uid);
  if (!r) return "You don't have that companion";
  if (Entity* live = stableLive(lv, uid)) {
    dismountAll(lv, *live);
    stableSync(*r, *live);
    poof(lv, *live);
    live->removed = true;
  }
  if ((*r).value("state", "") == "out") (*r)["state"] = "stabled";
  (*r)["gen"] = (*r).value("gen", 1) + 1;
  return "";
}

void stableRename(Level& lv, Player& p, const std::string& uid, const std::string& name) {
  json* r = stableRec(lv, p.name, uid);
  if (!r) return;
  std::string n = name.substr(0, 24);
  while (!n.empty() && isspace((unsigned char)n.back())) n.pop_back();
  while (!n.empty() && isspace((unsigned char)n.front())) n.erase(n.begin());
  (*r)["name"] = n.empty() ? json() : json(n);
  if (Entity* live = stableLive(lv, uid)) { if (n.empty()) live->data.erase("petName"); else live->data["petName"] = n; }
}

void stableRelease(Level& lv, Player& p, const std::string& uid) {
  json& book = stableBook(lv, p.name);
  for (size_t i = 0; i < book.size(); i++) {
    if (book[i].value("uid", "") != uid) continue;
    if (Entity* live = stableLive(lv, uid)) {
      dismountAll(lv, *live);
      for (const char* k : {"owner", "tamed", "kept", "uid", "petName", "sgen"}) live->data.erase(k);
      poof(lv, *live);
    }
    book.erase(book.begin() + i);
    return;
  }
}

void stableFav(Level& lv, Player& p, const std::string& uid) {
  for (auto& r : stableBook(lv, p.name)) r["fav"] = r.value("uid", "") == uid ? !r.value("fav", false) : false;
}

std::vector<StableEntry> stableList(Level& lv, const std::string& owner) {
  std::vector<StableEntry> out;
  double now = playTime(lv);
  for (auto& r : stableBook(lv, owner)) {
    if (r.value("state", "") == "resting" && now >= r.value("restUntil", 0.0)) { r["state"] = "stabled"; r["hp"] = r.value("maxHp", r.value("hp", 10.f)); }
    StableEntry c;
    c.uid = r.value("uid", ""); c.type = r.value("type", ""); c.state = r.value("state", "stabled");
    Entity* live = c.state == "out" ? stableLive(lv, c.uid) : nullptr;
    std::string nm = stableName(r);
    c.named = r.value("name", json()).is_string(); c.name = c.named ? nm : capFirst(nm); c.kind = label(c.type);
    c.variant = r.value("variant", json()).is_string() ? r["variant"].get<std::string>() : "";
    c.saddled = r.value("saddled", false); c.baby = r.value("baby", 0) != 0;
    c.hp = live ? live->hp : r.value("hp", 10.f);
    c.maxHp = live ? live->maxHp : r.value("maxHp", mobT(c.type).value("hp", 10.f));
    c.here = live != nullptr;
    c.x = live ? live->pos.x : r.value("x", 0.f); c.z = live ? live->pos.z : r.value("z", 0.f);
    c.dim = live ? jsDimOf(*live) : r.value("dim", 0);
    c.rest = c.state == "resting" ? std::max(0, (int)std::ceil(r.value("restUntil", 0.0) - now)) : 0;
    c.fav = r.value("fav", false); c.mount = stableIsMount(c.type);
    out.push_back(c);
  }
  return out;
}

std::string stableQuick(Level& lv, Player& p) {
  json& book = stableBook(lv, p.name);
  json* r = nullptr;
  for (auto& q : book) if (q.value("fav", false)) { r = &q; break; }
  if (!r) for (auto& q : book) if (stableIsMount(q.value("type", "")) && q.value("state", "") != "resting") { r = &q; break; }
  if (!r) return "No mount to call \xE2\x80\x94 mark one with \xE2\x98\x85 in your stable (H)";
  std::string uid = (*r).value("uid", "");
  Entity* live = stableLive(lv, uid);
  if (live && riderOf(lv, *live)) return "";
  if (live && std::hypot(live->pos.x - p.pos.x, live->pos.z - p.pos.z) < 8) return stableDismiss(lv, p, uid);
  return stableSummon(lv, p, uid);
}

bool stableCreatureDies(Level& lv, Entity& m) {
  if (lv.isRemote()) return false;
  std::string owner = sdata(m, "owner");
  json* r = stableRec(lv, owner, sdata(m, "uid"));
  if (!r) return false;
  stableSync(*r, m);
  (*r)["state"] = "resting"; (*r)["restUntil"] = playTime(lv) + STABLE_REST; (*r)["hp"] = (*r).value("maxHp", m.maxHp); (*r)["gen"] = (*r).value("gen", 1) + 1;
  if (Player* o = findPlayer(lv, owner)) toast(lv, *o, capFirst(stableName(*r)) + " is hurt and goes home to the stable to rest (5 min)", "warn");
  return true;
}

json stableListJson(const std::vector<StableEntry>& l) {
  json a = json::array();
  for (auto& c : l) a.push_back({{"uid", c.uid}, {"type", c.type}, {"name", c.name}, {"kind", c.kind}, {"state", c.state}, {"named", c.named}, {"saddled", c.saddled}, {"baby", c.baby},
                                 {"here", c.here}, {"fav", c.fav}, {"mount", c.mount}, {"hp", c.hp}, {"maxHp", c.maxHp}, {"x", c.x}, {"z", c.z}, {"dim", c.dim}, {"rest", c.rest}, {"variant", c.variant}});
  return a;
}
std::vector<StableEntry> stableListFromJson(const json& j) {
  std::vector<StableEntry> out;
  if (!j.is_array()) return out;
  for (auto& o : j) {
    StableEntry c;
    c.uid = o.value("uid", ""); c.type = o.value("type", ""); c.name = o.value("name", ""); c.kind = o.value("kind", ""); c.state = o.value("state", "");
    c.named = o.value("named", false); c.saddled = o.value("saddled", false); c.baby = o.value("baby", false); c.here = o.value("here", false);
    c.fav = o.value("fav", false); c.mount = o.value("mount", false); c.hp = o.value("hp", 0.f); c.maxHp = o.value("maxHp", 1.f);
    c.x = o.value("x", 0.f); c.z = o.value("z", 0.f); c.dim = o.value("dim", 0); c.rest = o.value("rest", 0); c.variant = o.value("variant", "");
    out.push_back(c);
  }
  return out;
}

bool stableAction(Level& lv, Player& p, const json& a) {
  std::string op = a.value("op", ""), uid = a.value("uid", ""), msg;
  if (op == "summon") msg = stableSummon(lv, p, uid);
  else if (op == "dismiss") msg = stableDismiss(lv, p, uid);
  else if (op == "rename") stableRename(lv, p, uid, a.value("name", ""));
  else if (op == "release") stableRelease(lv, p, uid);
  else if (op == "fav") stableFav(lv, p, uid);
  else if (op == "quick") msg = stableQuick(lv, p);
  if (!msg.empty()) toast(lv, p, msg);
  if (op != "quick") lv.fx->message(p.id, stableListJson(stableList(lv, p.name)).dump(), "stable-list");
  return true;
}

bool stableUse(UseCtx& u) {
  if (u.p.held().empty() || u.p.held().id != "whistle" || u.item != "whistle") return false;
  if (u.p.sneaking) {
    if (u.lv.isRemote()) contentAction(u.lv, u.p, "stable", {{"op", "quick"}});
    else { std::string m = stableQuick(u.lv, u.p); if (!m.empty()) toast(u.lv, u.p, m); }
  } else {
    sfxUi(u.lv, u.p, "whistle");
    openContentScreen(u.lv, u.p, "stable");
  }
  return true;
}

} // namespace dq
