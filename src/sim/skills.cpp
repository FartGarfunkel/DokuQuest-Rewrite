// sim/skills.cpp — game/skills.js (+ the perk effects of rpg.js that touch breaking, crafting and fighting).
#include "sim/skills.h"
#include "sim/rpg.h"
#include "sim/magic_data.h"
#include "sim/effects.h"

namespace dq {

static double rnd() { return mrand(); }

// ---- levels ------------------------------------------------------------------------------------------
double skillXpFor(int L) {
  const auto& t = magicData().skillXp;
  if (t.empty()) return 0;
  return t[clampv(L, 0, (int)t.size() - 1)];
}
int skillLevel(double xp) { int L = 1; while (L < 99 && xp >= skillXpFor(L + 1)) L++; return L; }
static const json& skillsJ(const Player& p) { static const json E = json::object(); return p.stats.contains("skills") && p.stats["skills"].is_object() ? p.stats["skills"] : E; }
double skillXp(const Player& p, const std::string& k) { const json& s = skillsJ(p); return s.contains(k) && s[k].is_number() ? s[k].get<double>() : 0; }
int skillLv(const Player& p, const std::string& k) { return skillLevel(skillXp(p, k)); }
float skillBonus(const Player& p, const std::string& k, float max) { return (skillLv(p, k) - 1) / 98.f * max; }
int totalLevel(const Player& p) { int t = 0; for (auto& s : magicData().skills) t += skillLv(p, s.key); return t; }

void skillGain(Level& lv, Player& p, const std::string& k, double xp) {
  if (xp <= 0 || p.creative()) return;
  if (pathSkill(p) == k) xp = std::round(xp * 1.1);   // paths.js
  int before = skillLv(p, k);
  json& S = p.stats["skills"];
  if (!S.is_object()) S = json::object();
  S[k] = std::min(200000000.0, skillXp(p, k) + xp);
  int after = skillLv(p, k);
  if (lv.events) lv.events->emit("skillXp", json::array({p.name, k, xp, after}));
  const MagicData& md = magicData();
  if (after > before) {
    lv.fx->message(p.id, md.skillName(k) + " level " + std::to_string(after) + "!", after % 10 == 0 || after == 99 ? "epic" : "good");
    lv.fx->soundUi(p.id, "levelup");
    if (!p.stats.value("skillHint", false)) { p.stats["skillHint"] = true; lv.fx->message(p.id, "Press K to see your skills", ""); }
  }
  auto it = md.perks.find(k);
  if (it != md.perks.end()) for (auto& pk : it->second) if (before < pk.lvl && after >= pk.lvl) {
    lv.fx->message(p.id, "Perk: " + pk.name + " \xE2\x80\x94 " + pk.desc, "epic");
    lv.fx->soundUi(p.id, "levelup");
  }
}

// ---- gear ---------------------------------------------------------------------------------------------
int gearLevelOf(double gx) { return std::min(GEAR_MAX, 1 + (int)std::floor(std::sqrt(std::max(0.0, gx) / 30))); }
double gearXpFor(int L) { return (double)(L - 1) * (L - 1) * 30; }
float gearBonus(const ItemStack& s, float max) {
  if (s.empty() || !s.tag.is_object() || !s.tag.contains("gx")) return 0;
  return (gearLevel(s) - 1) / (float)(GEAR_MAX - 1) * max;
}
void gearGain(Level& lv, Player& p, ItemStack& s, double n) {
  if (s.empty() || n <= 0) return;
  const ItemDef* d = s.def();
  if (!d || !(d->hasTool || d->has("armor") || d->has("bow"))) return;
  int before = gearLevel(s);
  if (!s.tag.is_object()) s.tag = json::object();
  s.tag["gx"] = std::floor(s.tag.value("gx", 0.0) + n);
  int after = gearLevel(s);
  if (after > before) { lv.fx->message(p.id, d->name + " is now level " + std::to_string(after), "good"); lv.fx->soundUi(p.id, "xp"); }
}

// ---- blocks -------------------------------------------------------------------------------------------
bool blockSkill(const BlockDef& b, BlockSkill& o) {
  const auto& ore = magicData().oreXp;
  auto it = ore.find(b.key);
  if (it != ore.end()) { o = {"mining", it->second, true}; return true; }
  if (b.has("isLog")) { o = {"woodcutting", 12, false}; return true; }
  if (b.tool == "pickaxe") { o = {"mining", std::max(1, (int)std::lround(b.hardness * 2)), false}; return true; }
  if (b.tool == "shovel") { o = {"excavation", b.key == "gravel" || b.key == "clay" ? 6 : 3, false}; return true; }
  if (b.tool == "axe") { o = {"woodcutting", 3, false}; return true; }
  return false;
}

float skillBreakDivisor(const Player& p, const BlockDef& b, const ItemStack& held) {
  BlockSkill sk;
  bool has = blockSkill(b, sk);
  const ItemDef* d = held.def();
  float k = 1 + (has ? skillBonus(p, sk.skill, 0.4f) : 0) + (d && d->hasTool ? gearBonus(held, 0.35f) : 0);
  // rpg.js perks
  if (b.tool == "pickaxe" && hasPerk(p, "mining", 60)) k *= 1.2f;
  else if (b.tool == "shovel" && hasPerk(p, "excavation", 90)) k *= 1.25f;
  return k * effectBreakDivisor(p);
}

static std::string seedFor(const BlockDef& b) {
  const json& c = b.props.value("crop", json());
  std::string s = c.is_object() ? c.value("seed", "") : "";
  if (!s.empty() && reg().item(s)) return s;
  for (auto& it : reg().items) if (it.str("plants") == b.key || (it.block == b.key && !it.has("food"))) return it.key;
  return "";
}

void onBlockBroken(Level& lv, Player& p, int x, int y, int z, const BlockDef& b, u8 meta, bool harvest, bool placed) {
  if (p.creative()) return;
  World& w = lv.world(p.realm);
  vec3 at(x + 0.5f, y + 0.5f, z + 0.5f);
  auto drop = [&](const std::string& k, int n = 1) { if (reg().item(k)) dropStack(lv, p.realm, ItemStack(k, n), at); };
  ItemStack& held = p.held();
  BlockSkill sk;
  bool has = blockSkill(b, sk);
  if (has && harvest && !placed) {
    skillGain(lv, p, sk.skill, sk.xp);
    gearGain(lv, p, held, sk.ore ? 4 : 1);
    if (sk.ore || sk.skill != "mining") {
      float chance = skillBonus(p, sk.skill, 0.25f) + gearBonus(held, 0.15f);
      if (rnd() < chance && !b.drop.empty()) drop(b.drop);
    }
  }
  const json& crop = b.props.value("crop", json());
  bool ripe = crop.is_object() && meta >= crop.value("stages", 1) - 1;
  if (ripe) {
    skillGain(lv, p, "farming", 10);
    if (rnd() < skillBonus(p, "farming", 0.4f) && crop.contains("drop")) drop(crop["drop"]);
  }
  if (placed) return;
  // rpg.js perks
  if (has && harvest && sk.ore && !b.drop.empty()) {
    if (hasPerk(p, "mining", 90) && rnd() < 0.05) drop(b.drop, 2);
    else if (hasPerk(p, "mining", 30) && rnd() < 0.1) drop(b.drop, 1);
  }
  if (has && sk.skill == "excavation" && harvest) {
    if (hasPerk(p, "excavation", 60) && rnd() < 0.012) drop(rnd() < 0.5 ? "emerald" : "bone");
    else if (hasPerk(p, "excavation", 30) && rnd() < 0.04) { static const char* k[] = {"flint", "clay_ball", "gold_nugget"}; drop(k[(int)(rnd() * 3) % 3]); }
  }
  if (ripe) {
    if (hasPerk(p, "farming", 60) && rnd() < 0.25 && crop.contains("drop")) drop(crop["drop"]);
    if (hasPerk(p, "farming", 90)) {
      std::string seed = seedFor(b);
      BlockId farmland = reg().blockId("farmland");
      if (!seed.empty() && w.getBlock(x, y, z) == AIR && w.getBlock(x, y - 1, z) == farmland) {
        for (auto& s : p.inv) if (s.id == seed && s.n > 0) { w.setBlock(x, y, z, b.id, 0); if (--s.n <= 0) s.clear(); break; }
      }
    }
  }
}

// ---- fighting -----------------------------------------------------------------------------------------
float skillHitFactor(const Player& p, const std::string& kind, const ItemStack& held) {
  if (kind.empty()) return 1;
  return 1 + skillBonus(p, kind, 0.2f) + (kind == "melee" ? gearBonus(held, 0.25f) : 0);
}
void onPlayerHitMob(Level& lv, Player& p, Entity& mob, const std::string& kind, float dealt, float hp0) {
  const json& T = mobTemplate(mob);
  if (kind.empty() || dealt <= 0 || (T.is_object() && T.value("passiveXp", false))) return;
  float bonus = isAlive(mob) ? 0 : (T.is_object() ? T.value("hp", 10.f) : 10.f);
  skillGain(lv, p, kind, std::round(std::min(dealt, std::max(0.f, hp0)) * 4 + bonus));
  if (kind == "melee") gearGain(lv, p, p.held(), std::ceil(dealt));
}

// ---- making things ------------------------------------------------------------------------------------
void onCrafted(Level& lv, Player& p, const std::string& out, int made, bool metal, bool alloy) {
  if (metal || alloy) skillGain(lv, p, "smithing", (metal ? 40 : 8) * std::max(1, made));
  // Fine Work / Masterwork / Legendary Smith: what you make starts with some wear-in
  int L = hasPerk(p, "smithing", 90) ? 10 : hasPerk(p, "smithing", 60) ? 6 : hasPerk(p, "smithing", 30) ? 3 : 0;
  const ItemDef* d = reg().item(out);
  bool m2 = d && (d->hasTool || d->has("armor")) && !d->has("staff") && !startsWith(out, "wood") && !startsWith(out, "leather") && !startsWith(out, "stone") && !startsWith(out, "gold");
  if (L && m2) {
    auto mark = [&](ItemStack& s) { if (s.id == out && !(s.tag.is_object() && s.tag.value("gx", 0.0) > 0)) { if (!s.tag.is_object()) s.tag = json::object(); s.tag["gx"] = gearXpFor(L); } };
    mark(p.cursor); for (auto& s : p.inv) mark(s);
  }
}
void onSmeltedTaken(Level& lv, Player& p, const std::string& id, int n) {
  const ItemDef* d = reg().item(id);
  if (n <= 0 || !d) return;
  if (d->has("food")) skillGain(lv, p, "cooking", 12.0 * n);
  else if (endsWith(id, "_ingot") || id == "iron_nugget" || id == "gold_nugget" || id == "copper_nugget") skillGain(lv, p, "smithing", 10.0 * n);
}
void onFishCaught(Level& lv, Player& p, const std::string& kind) {
  skillGain(lv, p, "fishing", kind == "treasure" ? 80 : kind == "fish" ? 40 : 15);
  gearGain(lv, p, p.held(), 3);
}
void onBred(Level& lv, Player& p) { skillGain(lv, p, "taming", 25); }
float cookingHealFactor(const Player& p) { return (1 + skillBonus(p, "cooking", 0.25f)) * (hasPerk(p, "cooking", 30) ? 1.1f : 1.f); }
bool smithSavesWear(const Player& p) { return rnd() < skillBonus(p, "smithing", 0.3f); }

} // namespace dq
