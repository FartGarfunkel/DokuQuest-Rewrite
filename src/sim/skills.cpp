// sim/skills.cpp — game/skills.js (+ the perk effects of rpg.js that touch breaking, crafting and fighting).
#include "sim/skills.h"
#include "sim/rpg.h"
#include "sim/balance.h"
#include "sim/gameplay.h"
#include <deque>
#include <unordered_set>
#include "sim/magic_data.h"
#include "sim/effects.h"
#include "sim/creature.h"

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
  const int totalBefore = totalLevel(p);
  json& S = p.stats["skills"];
  if (!S.is_object()) S = json::object();
  S[k] = std::min(200000000.0, skillXp(p, k) + xp);
  int after = skillLv(p, k);
  if (lv.events) lv.events->emit("skillXp", json::array({p.name, k, xp, after}));
  const MagicData& md = magicData();
  if (after > before) {
    lv.fx->message(p.id, md.skillName(k) + " skill level " + std::to_string(after) + "!", after % 10 == 0 || after == 99 ? "epic" : "good");
    // the total of your skills buys stat points (sim/balance.h): say so when one comes
    const int totalAfter = totalBefore + (after - before);
    if (totalAfter / bal::STAT_EVERY > totalBefore / bal::STAT_EVERY) lv.fx->message(p.id, "Skill total " + std::to_string(totalAfter) + ": a stat point to spend (Your Path)", "good");
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
  if (has && harvest && b.has("isLog") && woodcuttingExtraLog(p)) drop(b.drop.empty() ? b.key : b.drop);   // Lumberjack (a felled tree's: sim/controller.cpp)
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

void skillQueueSmelt(Player& p, const std::string& id, int n) {
  if (n <= 0 || id.empty()) return;
  json& q = p.data["smeltTaken"];
  if (!q.is_array()) q = json::array();
  q.push_back(json::array({id, n}));
}
void skillsTick(Level& lv, Player& p) {
  if (!p.data.is_object()) return;
  auto it = p.data.find("smeltTaken");
  if (it == p.data.end()) return;
  json q = std::move(*it); p.data.erase("smeltTaken");
  if (q.is_array()) for (auto& e : q) if (e.is_array() && e.size() == 2 && e[0].is_string() && e[1].is_number_integer()) onSmeltedTaken(lv, p, e[0].get<std::string>(), e[1].get<int>());
}
// the record of player-placed blocks lives in the chunks (world/chunk.h Chunk::placed), saved with them
void skillNotePlaced(Level& lv, int realm, int x, int y, int z) { lv.world(realm).setPlacedAt(x, y, z, true); }
bool skillTakePlaced(Level& lv, int realm, int x, int y, int z) {
  World& w = lv.world(realm);
  if (!w.placedAt(x, y, z)) return false;
  w.setPlacedAt(x, y, z, false);
  return true;
}
bool skillWasPlaced(Level& lv, int realm, int x, int y, int z) { return lv.world(realm).placedAt(x, y, z); }
float cookingHealFactor(const Player& p) { return (1 + skillBonus(p, "cooking", 0.25f)) * (hasPerk(p, "cooking", 30) ? 1.1f : 1.f); }
bool smithSavesWear(const Player& p) { return rnd() < skillBonus(p, "smithing", 0.3f); }

// ---- the perks the gameplay code asks about (rpg.js perks; their numbers: sim/balance.h) -----------------------------
void woodcuttingFelled(const Player& p, std::map<std::string, int>& logs, std::map<std::string, int>& extras, const std::string& sapling) {
  if (hasPerk(p, "woodcutting", 60))   // Lumberjack: a fifth more of each kind (the odd part of one by chance)
    for (auto& [k, n] : logs) { const double more = n * bal::PERK_LUMBERJACK; n += (int)more + (rnd() < more - std::floor(more) ? 1 : 0); }
  if (hasPerk(p, "woodcutting", 30) && !sapling.empty() && reg().item(sapling)) {   // Forester: always a sapling
    bool any = false;
    for (auto& [k, n] : extras) if (n > 0 && endsWith(k, "sapling")) any = true;
    if (!any) extras[sapling] = 1;
  }
}
bool woodcuttingExtraLog(const Player& p) { return hasPerk(p, "woodcutting", 60) && rnd() < bal::PERK_LUMBERJACK; }
int woodcuttingFellWear(const Player& p, int logs) {
  const double w = logs / 2.0;
  return (int)std::ceil(hasPerk(p, "woodcutting", 90) ? w * bal::PERK_HEARTWOOD : w);
}
bool woodcuttingSavesWear(const Player& p) { return hasPerk(p, "woodcutting", 90) && rnd() < 1 - bal::PERK_HEARTWOOD; }
float fishingBiteFactor(const Player& p) { return hasPerk(p, "fishing", 30) ? bal::PERK_PATIENT_ANGLER : 1.f; }
int fishingCatchCount(const Player& p, const std::string& kind) { return kind == "fish" && hasPerk(p, "fishing", 60) && rnd() < bal::PERK_DOUBLE_CATCH ? 2 : 1; }
float fishingTreasureFactor(const Player& p) { return hasPerk(p, "fishing", 90) ? bal::PERK_SEAS_FAVOUR : 1.f; }
float meleeCritBonus(const Player& p) { return hasPerk(p, "melee", 30) ? bal::PERK_KEEN_EDGE : 0.f; }
Entity* cleaveTarget(Level& lv, const Player& p, const Entity& target) {
  if (!hasPerk(p, "melee", 60) || target.isPlayer()) return nullptr;
  Entity* best = nullptr;
  float bestD = bal::PERK_CLEAVE_REACH;
  for (auto& up : lv.entities()) {
    Entity* e = up.get();
    if (e == &target || e->removed || e->realm != target.realm || e->hp <= 0 || e->isPlayer() || !isMob(*e)) continue;
    // a foe: a hostile creature, or one that's after you; never a creature someone keeps (yours or a friend's)
    const auto* c = dynamic_cast<const Creature*>(e);
    if (!c || !c->T || !c->owner.empty() || c->T->npc || !(c->T->hostile || (c->angryT > 0 && c->foe == p.id))) continue;
    const float d = glm::distance(e->pos, target.pos);
    if (d < bestD && glm::distance(e->pos, p.pos) < bal::PERK_CLEAVE_REACH + 3) { bestD = d; best = e; }
  }
  return best;
}
float bloodthirstHeal(const Player& p, float dealt) { return dealt > 0 && hasPerk(p, "melee", 90) ? dealt * bal::PERK_BLOODTHIRST : 0.f; }
float archerySpeedFactor(const Player& p) { return hasPerk(p, "archery", 30) ? bal::PERK_STEADY_AIM : 1.f; }
bool archeryKeepsArrow(const Player& p) { return hasPerk(p, "archery", 60) && rnd() < bal::PERK_RETRIEVAL; }
float archeryRangeFactor(const Player& p, float dist) { return dist >= bal::PERK_EAGLE_EYE_RANGE && hasPerk(p, "archery", 90) ? bal::PERK_EAGLE_EYE : 1.f; }
float tameChanceOf(const Player& p, float chance) { return hasPerk(p, "taming", 90) ? 1.f : chance; }
// the creature's keeper by name: in a joined world the host has the guest's mirror (their skills sent with it)
float petHarmFactor(Level& lv, const std::string& owner) {
  const Player* o = owner.empty() ? nullptr : playerNamed(lv, owner);
  return o && hasPerk(*o, "taming", 30) ? bal::PERK_BONDED : 1.f;
}
float petHitFactor(Level& lv, const std::string& owner) {
  const Player* o = owner.empty() ? nullptr : playerNamed(lv, owner);
  return o && hasPerk(*o, "taming", 60) ? bal::PERK_PACK_LEADER : 1.f;
}
float foodWaitFactor(const Player& p) { return hasPerk(p, "cooking", 60) ? bal::PERK_QUICK_BITE : 1.f; }
float feastRegenSecs(const Player& p) { return hasPerk(p, "cooking", 90) ? bal::PERK_FEAST_REGEN : 0.f; }
bool armourSavesWear(const Player& p) { return hasPerk(p, "defence", 90) && rnd() < bal::PERK_UNBREAKABLE; }
float exploringSpeedFactor(const Player& p) { return 1 + (hasPerk(p, "exploring", 30) ? bal::PERK_PATHFINDER : 0.f) + (hasPerk(p, "exploring", 90) ? bal::PERK_WAYFARER : 0.f); }

} // namespace dq
