// sim/enchanting.cpp — game/enchanting.js (+ arcana.js and deepdark.js enchantments, rpg.js Alchemy hooks).
#include "sim/enchanting.h"
#include "sim/magic_data.h"
#include "sim/magic_fx.h"
#include "sim/skills.h"
#include "sim/rpg.h"
#include "world/world.h"
#include <regex>

namespace dq {

static double rnd() { return mrand(); }
static const char* ARMOR_KINDS[4] = {"helmet", "chest", "legs", "boots"};

std::string enchName(const std::string& k, int lv) {
  const MagicData& md = magicData();
  const EnchDef* E = md.enchant(k);
  if (!E) return k;
  std::string r = lv >= 0 && lv < (int)md.roman.size() ? md.roman[lv] : std::to_string(lv);
  return E->name + (E->max > 1 || lv > 1 ? " " + r : "");
}

std::string enchKind(const std::string& id) {
  if (id == "book") return "book";
  if (id == "enchanted_book") return "ebook";
  const MagicData& md = magicData();
  auto it = md.enchItems.find(id);
  if (it != md.enchItems.end()) return it->second.first;
  const ItemDef* d = reg().item(id);   // a mod's item
  if (!d) return "";
  if (d->has("armor")) { int s = d->props["armor"].value("slot", 0); return ARMOR_KINDS[clampv(s, 0, 3)]; }
  if (d->hasTool) return d->tool.type == "fishing" ? "rod" : d->tool.type;
  return "";
}
bool enchApplies(const std::string& k, const std::string& kind) {
  const EnchDef* E = magicData().enchant(k);
  if (!E) return false;
  if (kind == "book" || kind == "ebook") return true;
  return std::find(E->on.begin(), E->on.end(), kind) != E->on.end();
}
bool enchCompatible(const json& en, const std::string& k) {
  const MagicData& md = magicData();
  const EnchDef* E = md.enchant(k);
  if (!E || !en.is_object()) return true;
  for (auto& [o, v] : en.items()) {
    if (o == k) continue;
    const EnchDef* O = md.enchant(o);
    if (!O) continue;
    if (!E->group.empty() && E->group == O->group) return false;
    if (std::find(E->excl.begin(), E->excl.end(), o) != E->excl.end() || std::find(O->excl.begin(), O->excl.end(), k) != O->excl.end()) return false;
  }
  return true;
}
json enchOf(ItemStack& s) {
  if (s.empty()) return nullptr;
  if (s.id == "enchanted_book" && !(s.tag.is_object() && s.tag.contains("en"))) {
    Mulberry32 r((u32)(rnd() * 4294967295.0));
    if (!s.tag.is_object()) s.tag = json::object();
    s.tag["en"] = enchRandomBook(r, 8 + (int)std::floor(rnd() * 24), true);
  }
  return s.tag.is_object() && s.tag.contains("en") ? s.tag["en"] : json();
}
int enchLv(const ItemStack& s, const std::string& k) {
  if (s.empty() || !s.tag.is_object()) return 0;
  auto it = s.tag.find("en");
  if (it == s.tag.end() || !it->is_object()) return 0;
  auto e = it->find(k);
  return e != it->end() && e->is_number() ? e->get<int>() : 0;
}
int enchArmorLv(const Player& p, const std::string& k) { int n = 0; for (auto& s : p.armor) n += enchLv(s, k); return n; }
int enchArmorMax(const Player& p, const std::string& k) { int n = 0; for (auto& s : p.armor) n = std::max(n, enchLv(s, k)); return n; }

int enchantability(const std::string& id) {
  const MagicData& md = magicData();
  auto it = md.enchItems.find(id);
  if (it != md.enchItems.end()) return it->second.second;
  std::string kind = enchKind(id);
  if (kind == "book") return 1;
  if (kind == "bow" || kind == "rod" || kind == "shears" || kind == "igniter") return 6;
  if (kind == "staff") return 18;
  std::smatch m; std::string mat;
  static const std::regex re("^([a-z]+)_");
  if (std::regex_search(id, m, re)) mat = m[1];
  const ItemDef* d = reg().item(id);
  bool armor = d && d->has("armor");
  static const std::unordered_map<std::string, int> A = {{"leather", 15}, {"chainmail", 12}, {"iron", 9}, {"gold", 25}, {"diamond", 10}, {"copper", 11}, {"bronze", 12}, {"silver", 18}, {"steel", 10}, {"mithril", 20}, {"adamantite", 12}, {"cinderite", 15}, {"dragonscale", 18}, {"mage", 22}, {"archmage", 24}, {"frost", 14}};
  static const std::unordered_map<std::string, int> T = {{"wooden", 15}, {"wood", 15}, {"stone", 5}, {"iron", 14}, {"gold", 22}, {"golden", 22}, {"diamond", 10}, {"copper", 12}, {"bronze", 13}, {"silver", 20}, {"steel", 11}, {"mithril", 22}, {"adamantite", 13}, {"cinderite", 16}};
  const auto& tab = armor ? A : T;
  auto f = tab.find(mat);
  return f == tab.end() ? 10 : f->second;
}

int enchLevelFor(const std::string& k, int P) {
  const EnchDef* E = magicData().enchant(k);
  if (!E || P < std::max(1, E->minP)) return 0;
  return clampv(1 + (int)std::floor((P - 1) * (double)E->max / 40), 1, E->max);
}
static std::pair<std::string, int> pickWeighted(const std::vector<std::pair<std::string, int>>& list, Mulberry32& r) {
  const MagicData& md = magicData();
  double t = 0; for (auto& e : list) t += md.ench.at(e.first).w;
  double q = r() * t;
  for (auto& e : list) { q -= md.ench.at(e.first).w; if (q <= 0) return e; }
  return list.back();
}
static int jsRound(double v) { return (int)std::floor(v + 0.5); }

// the picks in the order they were made (the original's object key order)
static json rollImpl(const std::string& kind, int L, int E, Mulberry32& r, std::vector<std::string>* order) {
  const MagicData& md = magicData();
  int P = L + 1 + (int)std::floor(r() * (E / 4.0 + 1)) + (int)std::floor(r() * (E / 4.0 + 1));
  P = std::max(1, jsRound(P * (1 + (r() + r() - 1) * 0.15)));
  std::vector<std::string> pool;
  for (auto& k : md.enchOrder) if (!md.ench.at(k).treasure && enchApplies(k, kind)) pool.push_back(k);
  json out = json::object();
  auto pick = [&](int pw) {
    std::vector<std::pair<std::string, int>> c;
    for (auto& k : pool) if (!out.contains(k) && enchCompatible(out, k)) { int lv = enchLevelFor(k, pw); if (lv > 0) c.push_back({k, lv}); }
    if (c.empty()) return false;
    auto e = pickWeighted(c, r);
    out[e.first] = e.second; if (order) order->push_back(e.first);
    return true;
  };
  if (!pick(P)) for (auto& k : pool) if (enchLevelFor(k, 1)) { out[k] = 1; if (order) order->push_back(k); break; }
  if (kind != "book") { int pw = P; while (r() < (pw + 1) / 50.0) { if (!pick(pw)) break; pw /= 2; } }
  return out;
}
json enchRoll(const std::string& kind, int L, int E, Mulberry32& r) { return rollImpl(kind, L, E, r, nullptr); }

json enchRandomBook(Mulberry32& r, int P, bool treasure) {
  const MagicData& md = magicData();
  std::vector<std::pair<std::string, int>> c;
  for (auto& k : md.enchOrder) {
    const EnchDef& E = md.ench.at(k);
    if (!treasure && E.treasure) continue;
    int lv = E.treasure ? 1 : enchLevelFor(k, P);
    if (lv > 0) c.push_back({k, lv});
  }
  if (c.empty()) return json::object();
  auto e = pickWeighted(c, r);
  return json{{e.first, e.second}};
}

bool enchOffers(const ItemStack& s, int shelves, u32 seed, std::array<EnchOffer, 3>& out) {
  if (s.empty()) return false;
  std::string kind = enchKind(s.id);
  if (kind.empty() || kind == "ebook" || (s.tag.is_object() && s.tag.contains("en") && !s.tag["en"].empty())) return false;
  u32 h = seed;
  for (unsigned char ch : s.id) h = (u32)((h ^ ch) * 2654435761u);
  Mulberry32 r(h);
  int n = std::min(15, shelves), E = enchantability(s.id);
  int base = 1 + (int)std::floor(r() * 8) + n / 2 + (int)std::floor(r() * (n + 1));
  int lv[3] = {std::max(base / 3, 1), base * 2 / 3 + 1, std::max(base, n * 2)};
  for (int i = 0; i < 3; i++) {
    std::vector<std::string> order;
    out[i].level = lv[i]; out[i].cost = i + 1;
    json en = rollImpl(kind, lv[i], E, r, &order);
    out[i].en = en; out[i].order = order;
  }
  return true;
}

int enchShelves(const World& w, int x, int y, int z) {
  BlockId shelf = reg().blockId("bookshelf"), cs = reg().blockId("bookcase");
  int n = 0;
  for (int dy = 0; dy <= 1; dy++) for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
    if (std::max(std::abs(dx), std::abs(dz)) != 2) continue;
    BlockId b = w.getBlock(x + dx, y + dy, z + dz);
    if (!b || (b != shelf && b != cs)) continue;
    int mx = x + (dx > 0 ? 1 : dx < 0 ? -1 : 0) * (std::abs(dx) == 2 ? 1 : 0), mz = z + (dz > 0 ? 1 : dz < 0 ? -1 : 0) * (std::abs(dz) == 2 ? 1 : 0);
    BlockId m = w.getBlock(mx, y + dy, mz);
    if (reg().tSolid[m] && reg().tOpaque[m]) continue;
    n++;
  }
  return n;
}

std::vector<int> enchRunes(u32 seed, int n) {
  Mulberry32 r(seed);
  int len = std::max(1, (int)magicData().runes.size());
  std::vector<int> out;
  for (int i = 0; i < n; i++) { if (i && r() < 0.2) out.push_back(-1); out.push_back((int)std::floor(r() * len)); }
  return out;
}
u32 enchSeed(Player& p) {
  if (!p.stats.value("enchSeed", 0)) p.stats["enchSeed"] = ((u32)(rnd() * 2147483647.0)) | 1u;
  return p.stats["enchSeed"].get<u32>();
}

std::string enchApplyOffer(Level& lv, Player& p, ItemStack& item, ItemStack& lapis, int i, int shelves, vec3 pos, ItemStack* bookLeftover) {
  std::array<EnchOffer, 3> offers;
  if (item.empty() || i < 0 || i > 2 || !enchOffers(item, shelves, enchSeed(p), offers)) return "";
  const EnchOffer& o = offers[i];
  bool cr = p.creative();
  int haveLapis = lapis.id == "lapis_lazuli" ? lapis.n : 0;
  if (!cr && (p.level < o.level || haveLapis < o.cost)) {
    lv.fx->soundUi(p.id, "click_off");
    return p.level < o.level ? "You need level " + std::to_string(o.level) : "That takes " + std::to_string(o.cost) + " lapis";
  }
  json en = o.en;
  int lv0 = p.level;
  if (item.id == "book") {
    int extra = item.n - 1;
    item = ItemStack("enchanted_book", 1); item.tag = json{{"en", en}};
    if (extra > 0 && bookLeftover) *bookLeftover = ItemStack("book", extra);
  } else { if (!item.tag.is_object()) item.tag = json::object(); item.tag["en"] = en; }
  if (!cr) { spendLevels(p, o.cost); lapis.n -= o.cost; if (lapis.n <= 0) lapis.clear(); }
  p.stats["enchSeed"] = ((u32)(rnd() * 2147483647.0)) | 1u;
  p.stats["enchanted"] = p.stats.value("enchanted", 0) + 1;
  lv.fx->sound("enchant", pos + vec3(0.5f, 1, 0.5f), 16);
  for (int k = 0; k < 26; k++) {
    float t = (float)rnd() * TAU, r = 0.3f + (float)rnd() * 0.6f;
    sfx::spark(lv, pos + vec3(0.5f + std::cos(t) * r, 1 + (float)rnd() * 0.8f, 0.5f + std::sin(t) * r), "ench_glyph_" + std::to_string(k % 6), json{{"vy", 0.6 + rnd()}, {"life", 0.9}, {"size", 0.12}});
  }
  onEnchanted(lv, p, std::max(1, lv0 - p.level));
  return "";
}

// ---- anvil / grindstone -----------------------------------------------------------------------------------
static bool enchMerge(const json& to, const json& from, const std::string& kind, json& outEn, int& cost) {
  const MagicData& md = magicData();
  outEn = to.is_object() ? to : json::object();
  int added = 0; cost = 0;
  if (!from.is_object()) return false;
  for (auto& [k, v] : from.items()) {
    const EnchDef* E = md.enchant(k);
    if (!E || !enchApplies(k, kind) || !enchCompatible(outEn, k)) continue;
    int lv = v.get<int>(), cur = outEn.value(k, 0);
    int nv = cur == lv ? std::min(E->max, lv + 1) : std::max(cur, lv);
    if (nv > cur) { outEn[k] = nv; added++; cost += nv * (E->w <= 2 ? 2 : 1); }
  }
  cost = std::max(1, cost);
  return added > 0;
}
StationResult anvilEnchant(ItemStack& a, ItemStack& b) {
  StationResult r;
  if (a.empty() || b.empty()) return r;
  std::string ka = enchKind(a.id);
  json eb = enchOf(b);
  bool same = a.id == b.id;
  if (ka.empty() || !eb.is_object() || eb.empty() || !(b.id == "enchanted_book" || same)) return r;
  json en; int cost;
  if (!enchMerge(enchOf(a), eb, ka, en, cost)) return r;
  r.out = a; r.out.n = 1;
  if (!r.out.tag.is_object()) r.out.tag = json::object();
  r.out.tag["en"] = en;
  r.useA = 1; r.useB = 1; r.lv = cost; r.ok = true;
  r.note = std::string("Adds the enchantment") + (eb.size() > 1 ? "s" : "") + " \xC2\xB7 costs " + std::to_string(cost) + " level" + (cost > 1 ? "s" : "");
  return r;
}
StationResult grindstoneDisenchant(ItemStack* a, ItemStack* b) {
  StationResult r;
  bool ha = a && !a->empty(), hb = b && !b->empty();
  ItemStack* one = ha && !hb ? a : hb && !ha ? b : nullptr;
  if (!one) return r;
  json en = enchOf(*one);
  if (!en.is_object() || en.empty()) return r;
  const MagicData& md = magicData();
  int xp = 0;
  for (auto& [k, v] : en.items()) { const EnchDef* E = md.enchant(k); xp += v.get<int>() * 3 + (E && E->treasure ? 6 : 0); }
  r.out = one->id == "enchanted_book" ? ItemStack("book", 1) : *one;
  r.out.n = 1;
  if (r.out.tag.is_object()) { r.out.tag.erase("en"); if (r.out.tag.empty()) r.out.tag = nullptr; }
  r.useA = one == a ? 1 : 0; r.useB = one == b ? 1 : 0; r.xp = xp; r.ok = true;
  r.note = "Takes the enchantments off \xC2\xB7 gives back " + std::to_string(xp) + " experience";
  return r;
}

// ---- what they do -----------------------------------------------------------------------------------------
float enchMiningBonus(const ItemStack& h) { int e = enchLv(h, "eff"); return e ? (float)(e * e + 1) : 0.f; }
float enchDamageBonus(const ItemStack& h, const Entity* t) {
  float b = 0;
  if (int s = enchLv(h, "sharp")) b += 0.5f * s + 0.5f;
  if (t) {
    if (int s = enchLv(h, "smite")) if (isUndead(*t)) b += 2.5f * s;
    if (int s = enchLv(h, "bane")) if (isArthropod(*t)) b += 2.5f * s;
  }
  return b;
}
float enchKnockFactor(const ItemStack& h) { int k = enchLv(h, "knock"); return 1 + 0.8f * k; }
float enchFireAspect(const ItemStack& h) { return 4.f * enchLv(h, "fire"); }

float protectionFactor(const Player& p, const std::string& t, const std::string& el) {
  int epf = enchArmorLv(p, "prot");
  if (el == "fire" || t == "lava" || t == "burn") epf += 2 * enchArmorLv(p, "fprot");
  if (t == "explosion") epf += 2 * enchArmorLv(p, "bprot");
  if (t == "arrow" || t == "projectile" || t == "magic_bolt") epf += 2 * enchArmorLv(p, "pprot");
  if (t == "fall") epf = 3 * enchArmorLv(p, "feather");
  return epf > 0 ? 1 - std::min(0.6f, epf * 0.025f) : 1.f;
}
bool enchUnbreakingSaves(const ItemStack& s) { int u = enchLv(s, "unb"); return u && !(rnd() < 1.0 / (u + 1)); }
bool enchArmorWearSaves(const ItemStack& s) { int u = enchLv(s, "unb"); return u && rnd() > 0.6 + 0.4 / (u + 1); }
int enchLootingExtra(const ItemStack& h, double ch) {
  int loot = enchLv(h, "loot"), n = 0;
  for (int k = 0; k < loot; k++) if (rnd() < std::min(1.0, ch + 0.1)) n += rnd() < 0.5 ? 1 : 0;
  return n;
}
bool enchBlockDrops(const ItemStack& held, const BlockDef& b, std::vector<ItemStack>& out) {
  static const std::unordered_set<std::string> NO = {"grave", "monster_spawner", "item_pile", "backpack_block", "pocket_wall", "fire", "soul_fire", "farmland"};
  if (enchLv(held, "silk") && reg().item(b.key) && !NO.count(b.key) && !b.has("crop") && !b.has("door") && !b.has("bed") && !b.has("double") &&
      b.shapeKind != ShapeKind::Slab && b.drop != b.key) { out.push_back(ItemStack(b.key, 1)); return true; }
  if (int f = enchLv(held, "fortune")) {
    static const std::regex ore("_ore$|^glowstone$");
    if (!b.drop.empty() && b.drop != b.key && (std::regex_search(b.key, ore) || b.has("dropCount"))) {
      int n = 1;
      const json& dc = b.props.value("dropCount", json());
      if (dc.is_array() && dc.size() == 2) n = dc[0].get<int>() + (int)std::floor(rnd() * (dc[1].get<int>() - dc[0].get<int>() + 1));
      int extra = std::max(0, (int)std::floor(rnd() * (f + 2)) - 1);
      out.push_back(ItemStack(b.drop, n + extra * std::max(1, jsRound(n / 2.0))));
      return true;
    }
  }
  return false;
}
std::string enchSmeltDrop(const ItemStack& h, const std::string& drop) {
  if (!enchLv(h, "smelt")) return "";
  auto it = reg().smelting.find(drop);
  return it != reg().smelting.end() && reg().item(it->second) ? it->second : "";
}
float enchAquaDivisor(const Player& p, bool eyeInWater) { return eyeInWater && enchArmorMax(p, "aqua") ? 3.f : 1.f; }
int enchBreathBonus(const Player& p) { return enchArmorLv(p, "resp"); }
float enchMoveFactor(const Player& p, bool inWater, bool sneaking, bool flying) {
  float k = 1;
  if (flying) return k;
  if (int ds = enchArmorMax(p, "strider")) if (inWater && !p.swimming) k *= 1 + ds * 0.39f;
  if (int s = enchLv(p.armor[3], "swiftboots")) k *= 1 + 0.06f * s;
  if (int s = enchLv(p.armor[2], "swift")) if (sneaking || p.crawling) k *= 1 + s * 0.45f;
  return k;
}
float enchArrowDamageFactor(const ItemStack& bow) { int pw = enchLv(bow, "power"); return pw ? 1 + 0.25f * (pw + 1) : 1.f; }
int enchArrowPunch(const ItemStack& bow) { return enchLv(bow, "punch"); }
bool enchArrowFlame(const ItemStack& bow) { return enchLv(bow, "flame") > 0; }
bool enchInfinity(const ItemStack& bow) { return enchLv(bow, "inf") > 0; }
bool enchVolley(const ItemStack& bow) { return enchLv(bow, "volley") > 0; }
int enchLure(const ItemStack& rod) { return enchLv(rod, "lure"); }
int enchLuck(const ItemStack& rod) { return enchLv(rod, "luck"); }

void enchAfterMelee(Level& lv, Player& p, Entity& t, float dealt) {
  if (dealt <= 0) return;
  const ItemStack& h = p.held();
  if (int b = enchLv(h, "bane")) if (isArthropod(t)) setMobStatus(lv, t, "slowT", 1 + (float)rnd() * 0.5f * b, &p);
  if (int f = enchLv(h, "fire")) if (isAlive(t) && !isFireImmune(t)) setMobStatus(lv, t, "burnT", 4.f * f, &p);
  if (int v = enchLv(h, "vamp")) combatHeal(lv, p, dealt * 0.04f * v);
  if (int f = enchLv(h, "frostasp")) if (isAlive(t)) {
    setMobStatus(lv, t, "slowT", 2 + 2.f * f, &p);
    sfx::burst(lv, t.pos + vec3(0, t.h * 0.6f, 0), "mana_frost", 6, 0.8f, json{{"life", 0.5}});
  }
}
void enchThorns(Level& lv, Player& p, Entity* a, float before) {
  int th = enchArmorMax(p, "thorns");
  if (!th || !a || !isMob(*a) || !isAlive(*a) || p.hp >= before || !(rnd() < 0.15 * th + 0.1)) return;
  combatHurt(lv, *a, 1 + (float)std::floor(rnd() * 3), json{{"knock", 1}, {"type", "thorns"}}, &p);
  lv.fx->sound("hit", a->pos + vec3(0, a->h / 2, 0));
  for (auto& s : p.armor) if (enchLv(s, "thorns")) s.d++;
}
int enchXpGain(Player& p, int n, bool* mended) {
  if (int lv = enchLv(p.armor[0], "insight")) n = jsRound(n * (1 + 0.15 * lv));
  if (mended) *mended = false;
  std::vector<ItemStack*> worn;
  auto add = [&](ItemStack& s) { if (!s.empty() && enchLv(s, "mend") && s.d > 0) worn.push_back(&s); };
  add(p.held()); add(p.offhand[0]); for (auto& s : p.armor) add(s);
  if (!worn.empty() && n > 0) {
    ItemStack& s = *worn[(size_t)(rnd() * worn.size()) % worn.size()];
    int fix = std::min(s.d, n * 2);
    s.d -= fix; n -= (fix + 1) / 2;
    if (n <= 0 && mended) *mended = true;
  }
  return std::max(0, n);
}

} // namespace dq
