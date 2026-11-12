// sim/magic_hands.cpp — the hands. A spell readied in each hand (the spellbook: the right hand is the active spell,
// the left hand is set with X / right-click). While the left hand is free (empty, or a staff in it) the left-hand
// spell joins every cast: the same spell in both hands is one dual cast (stronger, dearer); two different spells go
// off together, a little cheaper, and some pairs fuse where you look — fire and frost make scalding steam, fire and
// storm plasma, frost and storm hail, life renews you, death drinks, nature snares; two of one school resonate.
// A sword or axe in the right hand: use casts the left-hand spell, and the blade carries it for a few seconds.
// Every spell ranks up I..X with use: each rank a little stronger and cheaper.
#include "sim/magic.h"
#include "sim/magic_fx.h"
#include "sim/player.h"
#include "sim/skills.h"
#include "core/registry.h"

namespace dq {

namespace {
const char* ROMAN[11] = {"", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
double spellXp(const Player& p, const std::string& k) {
  auto it = p.stats.find("spellXp");
  if (it == p.stats.end() || !it->is_object()) return 0;
  auto jt = it->find(k);
  return jt != it->end() && jt->is_number() ? jt->get<double>() : 0;
}
int rankOf(double xp) { return std::min(10, 1 + (int)std::sqrt(std::max(0.0, xp) / 40)); }
bool staffIn(const ItemStack& s) { const ItemDef* d = s.def(); return d && d->props.contains("staff") && d->props["staff"].is_object(); }
bool weaponIn(const ItemStack& s) { const ItemDef* d = s.def(); return d && d->hasTool && (d->tool.type == "sword" || d->tool.type == "axe"); }
bool leftFree(const Player& p) { return p.offhand[0].empty() || staffIn(p.offhand[0]); }
vec3 lookOf(const Player& p) { return vec3(-std::sin(p.yaw) * std::cos(p.pitch), std::sin(p.pitch), -std::cos(p.yaw) * std::cos(p.pitch)); }

// where the fusion lands: the first foe or block along your gaze (16 blocks), else the far end
vec3 gazePoint(Level& lv, Player& p) {
  World& w = lv.world(p.realm);
  const vec3 E = p.pos + vec3(0, 1.55f, 0), L = lookOf(p);
  for (float d = 1.5f; d <= 16; d += 0.5f) {
    vec3 v = E + L * d;
    for (auto& e : lv.entities()) if (e->realm == p.realm && isFoe(*e) && std::abs(e->pos.x - v.x) < e->w + 0.4f && std::abs(e->pos.z - v.z) < e->w + 0.4f && v.y > e->pos.y - 0.2f && v.y < e->pos.y + e->h + 0.3f) return e->pos + vec3(0, e->h * 0.5f, 0);
    if (reg().tSolid[w.getBlock(ifloor(v.x), ifloor(v.y), ifloor(v.z))]) return E + L * (d - 0.5f);
  }
  return E + L * 16.f;
}

struct Fusion { const char* name = nullptr; const char* kind = ""; float mul = 1; };
Fusion fusionOf(const std::string& a, const std::string& b) {
  const std::string sa = schoolOfSpell(a), sb = schoolOfSpell(b);
  auto pair = [&](const char* x, const char* y) { return (sa == x && sb == y) || (sa == y && sb == x); };
  auto any = [&](const char* x) { return sa == x || sb == x; };
  if (sa == sb && !sa.empty()) return {"Resonance", "resonance", 1.25f};
  if (pair("fire", "frost")) return {"Steam", "steam", 1};
  if (pair("fire", "storm")) return {"Plasma", "plasma", 1};
  if (pair("frost", "storm")) return {"Hail", "hail", 1};
  if (any("life")) return {"Renewal", "renewal", 1};
  if (any("death")) return {"Drain", "drain", 1};
  if (any("nature")) return {"Bramble", "bramble", 1};
  if (any("arcane")) return {"Arcane surge", "surge", 1.2f};
  return {};
}

void fusionBurst(Level& lv, Player& p, const Fusion& f, float power) {
  if (!f.name) return;
  lv.fx->particles("text", p.pos + vec3(0, 2.2f, 0), 1, json{{"text", f.name}, {"col", "#e0c8ff"}});
  const std::string k = f.kind;
  if (k == "resonance" || k == "surge") { sfx::spiral(lv, p.pos, "mana_arcane", 30, json{{"r", 0.7}, {"h", 2.2}, {"turns", 2}, {"life", 0.7}}); return; }
  if (k == "renewal") {
    combatHeal(lv, p, 3 * power);
    for (auto& e : lv.entities()) if (e->isPlayer() && e.get() != &p && e->realm == p.realm && glm::length(e->pos - p.pos) < 6) combatHeal(lv, *e, 2 * power);
    sfx::burst(lv, p.pos + vec3(0, 1, 0), "mana_heal", 24, 1.2f, json{{"life", 0.8}, {"spread", 1}});
    lv.fx->sound("spell_heal", p.pos, 16);
    return;
  }
  const vec3 at = gazePoint(lv, p);
  struct K { const char* k; float r, dmg; const char* el; const char* status; float secs; const char* fx; };
  static const K table[] = {
    {"steam", 3.0f, 4, "", "slowT", 4, "mana_light"},
    {"plasma", 2.5f, 6, "fire", "burnT", 4, "mana_fire"},
    {"hail", 3.5f, 4, "frost", "slowT", 6, "mana_frost"},
    {"drain", 3.0f, 3, "", nullptr, 0, "mana_arcane"},
    {"bramble", 3.0f, 2, "", "slowT", 8, "mana_heal"},
  };
  const K* t = nullptr;
  for (auto& x : table) if (k == x.k) t = &x;
  if (!t) return;
  float drank = 0;
  for (auto& e : lv.entities()) {
    if (e->realm != p.realm || !isFoe(*e) || glm::length(e->pos - at) > t->r + e->w) continue;
    json src{{"type", "magic"}, {"knock", 2}};
    if (*t->el) src["element"] = t->el;
    drank += combatHurt(lv, *e, t->dmg * power, src, &p);
    if (t->status) setMobStatus(lv, *e, t->status, t->secs, &p);
  }
  if (k == "drain" && drank > 0) combatHeal(lv, p, drank * 0.5f);
  if (k == "steam") lv.fx->particles("smoke", at, 24, json{{"spread", t->r}});
  sfx::burst(lv, at, t->fx, 30, 2.2f, json{{"life", 0.7}, {"spread", t->r * 0.6f}});
  sfx::ring(lv, at, t->fx, t->r, 24, json{{"speed", 2}, {"life", 0.5}, {"flat", true}, {"size", 0.08}});
  lv.fx->sound(k == "plasma" ? "spell_fire" : k == "hail" ? "spell_frost" : "spell_hit", at, 24);
}

// the left hand casts k: its own staff if it holds one (swapped into the right hand for the cast), else bare
bool castLeft(Level& lv, Player& p, const std::string& k, float power, float mana) {
  MagicState& m = magicState(lv, p);
  const bool staff = staffIn(p.offhand[0]);
  if (!staff && spellNeedsStaff(k)) {
    const SpellDef* S = magicData().spell(k);
    lv.fx->message(p.id, "Hold a staff in your left hand to cast " + (S ? S->name : k), "");
    return false;
  }
  HandMods& H = handMods();
  H = {}; H.power = power; H.mana = mana; H.noStaff = !staff;
  const bool bare0 = m.bare;
  m.bare = !staff;
  if (staff) std::swap(p.inv[p.selected], p.offhand[0]);
  castSpellKey(lv, p, k);
  if (staff) std::swap(p.inv[p.selected], p.offhand[0]);
  m.bare = bare0;
  const bool ok = H.ok;
  H = {};
  return ok;
}
} // namespace

// ---- ranks ------------------------------------------------------------------------------------------------
int spellRank(const Player& p, const std::string& k) { return rankOf(spellXp(p, k)); }
float spellRankProgress(const Player& p, const std::string& k) {
  double xp = spellXp(p, k); int r = rankOf(xp);
  if (r >= 10) return 1;
  double lo = 40.0 * (r - 1) * (r - 1), hi = 40.0 * r * r;
  return (float)clampv((xp - lo) / (hi - lo), 0.0, 1.0);
}
float spellRankPower(const Player& p, const std::string& k) { return 1 + 0.05f * (spellRank(p, k) - 1); }
float spellRankMana(const Player& p, const std::string& k) { return 1 - 0.03f * (spellRank(p, k) - 1); }
void spellPractice(Level& lv, Player& p, const std::string& k, double xp) {
  json& X = p.stats["spellXp"];
  if (!X.is_object()) X = json::object();
  const int before = rankOf(X.value(k, 0.0));
  X[k] = X.value(k, 0.0) + xp;
  const int after = rankOf(X[k].get<double>());
  if (after > before) {
    const SpellDef* S = magicData().spell(k);
    lv.fx->message(p.id, (S ? S->name : k) + " " + ROMAN[after] + (after == 10 ? " \xE2\x80\x94 mastered!" : "!"), after == 10 ? "epic" : "good");
    lv.fx->soundUi(p.id, "levelup");
    sfx::spiral(lv, p.pos, S ? S->fx : "mana_arcane", 24, json{{"r", 0.6}, {"h", 2.2}, {"turns", 2}, {"life", 0.8}});
  }
}
std::string spellTitle(const Player& p, const std::string& k) {
  const SpellDef* S = magicData().spell(k);
  const int r = spellRank(p, k);
  return (S ? S->name : k) + (r > 1 ? std::string(" ") + ROMAN[r] : "");
}

// ---- the left hand ------------------------------------------------------------------------------------------
std::string offSpell(const Player& p) {
  auto it = p.stats.find("spellOff");
  if (it == p.stats.end() || !it->is_string()) return "";
  std::string k = *it;
  auto& ks = knownSpells(p);
  return std::find(ks.begin(), ks.end(), k) != ks.end() ? k : "";
}
void setOffSpell(Level& lv, Player& p, const std::string& k) {
  const SpellDef* S = magicData().spell(k);
  if (k.empty() || !S || offSpell(p) == k) { p.stats.erase("spellOff"); lv.fx->message(p.id, "Your left hand is empty", ""); }
  else {
    p.stats["spellOff"] = k;
    lv.fx->message(p.id, S->name + " is in your left hand" + (spellNeedsStaff(k) ? " (hold a staff in it)" : ""), "good");
  }
  lv.fx->soundUi(p.id, "click");
}

// ---- casting with both hands (a staff in the right hand, or Z) ----------------------------------------------------
bool castSpell(Level& lv, Player& p) {
  HandMods& H = handMods();
  MagicState& m = magicState(lv, p);
  const std::string right = activeSpell(p), left = offSpell(p);
  H = {};
  if (right.empty() || left.empty() || !leftFree(p) || m.castCd > 0) return castSpellKey(lv, p, right);
  if (left == right) {   // both hands, one spell
    H.power = 1.6f; H.mana = 1.8f;
    bool r = castSpellKey(lv, p, right);
    if (H.ok) {
      const SpellDef* S = magicData().spell(right);
      lv.fx->particles("text", p.pos + vec3(0, 2.2f, 0), 1, json{{"text", "Dual cast"}, {"col", "#e0c8ff"}});
      sfx::ring(lv, p.pos + vec3(0, 0.2f, 0), S ? S->fx : "mana_arcane", 1.4f, 28, json{{"speed", 2.5}, {"life", 0.5}, {"flat", true}, {"size", 0.09}});
    }
    H = {};
    return r;
  }
  if (!staffIn(p.offhand[0]) && spellNeedsStaff(left)) return castSpellKey(lv, p, right);   // that one needs a staff in the left hand
  const Fusion f = fusionOf(right, left);
  H.power = f.mul; H.mana = 0.9f;
  bool r = castSpellKey(lv, p, right);
  if (!H.ok) { H = {}; return r; }
  const float cd1 = m.castCd;
  m.castCd = 0;
  const bool ok = castLeft(lv, p, left, f.mul, 0.9f);
  m.castCd = std::max(cd1, m.castCd);
  if (ok) {
    const float power = 1 + (skillLv(p, "magic") - 1) / 98.f * 0.6f;
    fusionBurst(lv, p, f, power * (spellRankPower(p, right) + spellRankPower(p, left)) * 0.5f);
  }
  return r;
}

// ---- one hand alone (the cast buttons: one for each hand; both at once cast with both) ---------------------------
void castHand(Level& lv, Player& p, int hand) {
  if (hand == 2) { castQuick(lv, p); return; }   // both hands: the dual cast / fusion
  MagicState& m = magicState(lv, p);
  if (hand == 1) {
    const std::string left = offSpell(p);
    if (left.empty()) { lv.fx->message(p.id, "Your left hand holds no spell (choose one in the spellbook)", ""); return; }
    if (!leftFree(p)) { lv.fx->message(p.id, "Your left hand is full", ""); return; }
    if (m.castCd > 0) return;
    castLeft(lv, p, left, 1, 1);
    return;
  }
  const std::string right = activeSpell(p);
  if (right.empty()) { lv.fx->message(p.id, "You know no spells yet \xE2\x80\x94 read a spell tome", ""); return; }
  if (m.castCd > 0) return;
  const bool staff = staffIn(p.held());
  if (!staff && spellNeedsStaff(right)) { const SpellDef* S = magicData().spell(right); lv.fx->message(p.id, "Hold a staff to cast " + (S ? S->name : right), ""); return; }
  HandMods& H = handMods(); H = {}; H.noStaff = !staff;
  const bool bare0 = m.bare; m.bare = !staff;
  castSpellKey(lv, p, right);
  m.bare = bare0; H = {};
}

// ---- a weapon in the right hand: the left hand casts, the blade carries it --------------------------------------------
bool castWithWeapon(Level& lv, Player& p) {
  if (!weaponIn(p.held())) return false;
  const std::string left = offSpell(p);
  if (left.empty() || !leftFree(p)) return false;
  MagicState& m = magicState(lv, p);
  if (m.castCd > 0) return true;
  if (!castLeft(lv, p, left, 1, 1)) return true;
  const SpellDef* S = magicData().spell(left);
  if (S && !S->util) {
    const std::string sc = S->school;
    if (sc != "life" && sc != "divine" && sc != "alter" && sc != "illusion") {
      if (m.bladeEl != sc || m.bladeUntil < unixMillis()) lv.fx->message(p.id, "Your blade carries " + S->name, "");
      m.bladeEl = sc; m.bladeUntil = unixMillis() + 6000;
    }
  }
  return true;
}

float spellbladeHit(Level& lv, Player& p, Entity& e, float dmg, std::string& element) {
  MagicState& m = magicState(lv, p);
  if (m.bladeEl.empty() || m.bladeUntil < unixMillis() || !weaponIn(p.held())) return dmg;
  const std::string& sc = m.bladeEl;
  const vec3 at = e.pos + vec3(0, e.h * 0.6f, 0);
  if (sc == "fire") { if (element.empty()) element = "fire"; dmg += 1; sfx::burst(lv, at, "mana_fire", 8, 1.2f); }
  else if (sc == "frost") { if (element.empty()) element = "frost"; dmg += 1; sfx::burst(lv, at, "mana_frost", 8, 1.2f); }
  else if (sc == "storm") { dmg += 2; sfx::burst(lv, at, "mana_light", 10, 2.f); lv.fx->sound("spell_zap", at, 12); }
  else if (sc == "death") { combatHeal(lv, p, 1); sfx::burst(lv, at, "mana_arcane", 8, 1.f); }
  else if (sc == "nature") { dmg += 1; setMobStatus(lv, e, "slowT", 3, &p); sfx::burst(lv, at, "mana_heal", 8, 1.f); }
  else { dmg += 1.5f; sfx::burst(lv, at, "mana_arcane", 8, 1.2f); }
  return dmg;
}

} // namespace dq
