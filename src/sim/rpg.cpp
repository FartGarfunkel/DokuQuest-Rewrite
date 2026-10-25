// sim/rpg.cpp — game/rpg.js: perks, Exploring and Alchemy experience, Realm Fragments, the journal; paths.js lookups.
#include "sim/rpg.h"
#include "sim/magic.h"
#include "sim/magic_data.h"
#include "sim/magic_fx.h"
#include "world/world.h"

namespace dq {

static double rnd() { return mrand(); }
static int keysOf(const json& j, const char* k) { return j.contains(k) && j[k].is_object() ? (int)j[k].size() : 0; }
static const json& sub(const json& j, const char* k) { static const json E = json::object(); return j.is_object() && j.contains(k) && j[k].is_object() ? j[k] : E; }

bool hasPerk(const Player& p, const std::string& k, int lvl) { return skillLv(p, k) >= lvl; }

static const json* pathOf(const Player& p) {
  const json& P = sub(p.stats, "path");
  return P.contains("k") && P["k"].is_string() && magicData().paths.contains(P["k"].get<std::string>()) ? &P : nullptr;
}
bool onPath(const Player& p, const std::string& k) { const json* P = pathOf(p); return P && (*P)["k"] == k; }
bool hasTal(const Player& p, const std::string& t) { const json* P = pathOf(p); return P && P->contains("t") && (*P)["t"].is_object() && (*P)["t"].contains(t) && (*P)["t"][t] != false; }
std::string pathSkill(const Player& p) { const json* P = pathOf(p); return P ? magicData().paths[(*P)["k"].get<std::string>()].value("skill", "") : ""; }

// the original's dimension numbers (DIM_LABEL keys): 0 overworld, 1 depths, 2 skylands, 3 pocket, 4 his realm, 5 the End
static int jsDimOf(const Player& p) { return p.realm == REALM_POCKET ? 3 : p.realm == REALM_HIS ? 4 : jsFromDim(dimOfY(ifloor(p.pos.y))); }

static std::function<std::string(Level&, int, int)> g_biomeNamer;
void setBiomeNamer(std::function<std::string(Level&, int, int)> f) { g_biomeNamer = std::move(f); }

// ---- Exploring (exploreTick) -------------------------------------------------------------------------------
static void exploreTick(Level& lv, Player& p, MagicState& m, double dt) {
  if (p.creative() || p.dead) return;
  if ((m.exT -= (float)dt) > 0) return;
  m.exT = 2;
  json& st = p.stats;
  json& E = st["explore"];
  if (!E.is_object()) E = json{{"v", 0}, {"d", 0}, {"s", 0}, {"dims", json::object()}, {"biomes", json::object()}, {"walked", 0}, {"at", nullptr}};
  double xp = 0;
  int nv = keysOf(st, "vseen"), nd = keysOf(st, "dseen"), ns = keysOf(st, "sseen");
  xp += std::max(0, nv - E.value("v", 0)) * 150 + std::max(0, nd - E.value("d", 0)) * 120 + std::max(0, ns - E.value("s", 0)) * 100;
  E["v"] = nv; E["d"] = nd; E["s"] = ns;
  int dim = jsDimOf(p);
  std::string ds = std::to_string(dim);
  if (!E["dims"].is_object()) E["dims"] = json::object();
  if (!E["dims"].contains(ds)) {
    E["dims"][ds] = 1;
    if (dim != 0) { xp += 400; lv.fx->message(p.id, "A new world: " + magicData().dimLabel.value(ds, std::string("somewhere else")), "good"); }
  }
  if (dim == 0 && g_biomeNamer) {
    std::string bn = g_biomeNamer(lv, ifloor(p.pos.x), ifloor(p.pos.z));
    if (!bn.empty() && !E["biomes"].contains(bn)) { E["biomes"][bn] = 1; if (E["biomes"].size() > 1) { xp += 60; lv.fx->message(p.id, "New land: " + bn, ""); } }
  }
  const json& at = E["at"];
  if (at.is_array() && at.size() == 4 && at[3].get<int>() == dim) { double d = std::hypot(p.pos.x - at[0].get<double>(), p.pos.z - at[2].get<double>()); if (d < 60) E["walked"] = E.value("walked", 0.0) + d; }
  E["at"] = {p.pos.x, p.pos.y, p.pos.z, dim};
  int steps = (int)std::floor(E.value("walked", 0.0) / 64);
  if (steps > 0) { E["walked"] = E.value("walked", 0.0) - steps * 64; xp += steps * 4; st["walkedKm"] = st.value("walkedKm", 0.0) + steps * 0.064; }
  if (xp > 0) skillGain(lv, p, "exploring", xp);
}

// ---- Green Thumb (farming 30) ------------------------------------------------------------------------------
static void greenThumb(Level& lv, Player& p, MagicState& m, double dt) {
  if (!hasPerk(p, "farming", 30) || lv.isRemote()) return;
  if ((m.gtT -= (float)dt) > 0) return;
  m.gtT = 1.5f;
  World& w = lv.world(p.realm);
  for (int i = 0; i < 10; i++) {
    int x = ifloor(p.pos.x + (rnd() - 0.5) * 14), z = ifloor(p.pos.z + (rnd() - 0.5) * 14), y = ifloor(p.pos.y + (rnd() - 0.5) * 4);
    BlockId id = w.getBlock(x, y, z);
    const BlockDef& b = reg().block(id);
    const json& crop = b.props.value("crop", json());
    if (!crop.is_object()) continue;
    u8 mm = w.getMeta(x, y, z), s = mm & 15;
    if (s < crop.value("stages", 1) - 1 && rnd() < 0.35) { w.setBlock(x, y, z, id, (u8)((mm & ~15) | (s + 1))); lv.fx->particles("happy", vec3(x + 0.5f, y + 0.6f, z + 0.5f), 2, json{{"speed", 0.3}}); }
  }
}

// ---- the journal's "Iron and Fire" ----------------------------------------------------------------------------
static void storyTick(Level& lv, Player& p, MagicState& m, double dt) {
  if ((m.stT -= (float)dt) > 0) return;
  m.stT = 3;
  json& S = p.stats["story"];
  if (!S.is_object()) S = json::object();
  if (S.value("iron", 0)) return;
  bool pick = false, arm = false;
  auto look = [&](const ItemStack& s) {
    const ItemDef* d = s.def(); if (!d) return;
    if (d->hasTool && d->tool.type == "pickaxe" && d->tool.tier >= 3) pick = true;
    if (d->has("armor") && !startsWith(s.id, "leather")) arm = true;
  };
  for (auto& s : p.inv) look(s);
  for (auto& s : p.armor) look(s);
  look(p.offhand[0]);
  if (pick && arm) { S["iron"] = 1; lv.fx->message(p.id, "Journal: Iron and Fire \xE2\x80\x94 done", "good"); }
}

void rpgTick(Level& lv, Player& p, double dt) {
  MagicState& m = magicState(lv, p);
  exploreTick(lv, p, m, dt);
  greenThumb(lv, p, m, dt);
  storyTick(lv, p, m, dt);
}

// ---- Alchemy -----------------------------------------------------------------------------------------------------
void onBrewDone(Level& lv, int realm, vec3 at, int made, ItemStack* sample) {
  Player* best = nullptr; float bd = 12;
  for (auto& e : lv.entities()) {
    if (e->removed || !e->isPlayer() || e->realm != realm) continue;
    Player* q = static_cast<Player*>(e.get());
    float d = glm::length(vec2(q->pos.x - at.x, q->pos.z - at.z));
    if (!q->dead && d < bd) { bd = d; best = q; }
  }
  if (!best) return;
  skillGain(lv, *best, "alchemy", 35.0 * made);
  if (hasPerk(*best, "alchemy", 60) && rnd() < 0.25 && sample && !sample->empty()) dropStack(lv, realm, ItemStack(sample->id, 1), at + vec3(0, 0.5f, 0));
}
void onEnchanted(Level& lv, Player& p, int spent) {
  skillGain(lv, p, "alchemy", 40 + spent * 30.0);
  if (hasPerk(p, "alchemy", 90) && spent > 0 && !p.creative()) p.level += 1;   // Philosopher
}

// ---- Realm Fragments from the great foes --------------------------------------------------------------------------
void onGreatFoeDied(Level& lv, Entity& mob) {
  const json& F = magicData().fragFoes;
  bool lair = mob.data.value("lair", false) && endsWith(mob.type, "_dragon");
  if (lv.isRemote() || mob.data.value("dungeon", false) || !(F.contains(mob.type) || lair)) return;
  json& R = lv.info.data["rift"];
  if (!R.is_object()) R = json{{"stage", 0}, {"frags", 0}};
  json& flags = lv.info.data["flags"];
  if (!flags.is_object()) flags = json::object();
  json& got = flags["fragFoes"];
  if (!got.is_object()) got = json::object();
  if (R.value("sealed", false) || got.contains(mob.type)) return;
  got[mob.type] = 1;
  if (R.value("stage", 0) >= 3) { if (reg().item("rift_crystal")) dropStack(lv, mob.realm, ItemStack("rift_crystal", 3), mob.pos + vec3(0, 1, 0)); return; }
  dropStack(lv, mob.realm, ItemStack("realm_fragment", 1), mob.pos + vec3(0, 1, 0));
  int frags = R.value("frags", 0) + 1;
  R["frags"] = frags;
  std::string who = F.value(mob.type, std::string("it"));
  if (lv.events) lv.events->emit("riftChanged", json::array());
  lv.fx->message(0, "A Realm Fragment falls from " + who + ". (" + std::to_string(std::min(4, frags)) + " of 4)", "epic");
}
std::vector<std::string> greatFoesBeaten(Level& lv) {
  std::vector<std::string> out;
  const json& F = magicData().fragFoes;
  for (auto& [k, v] : sub(sub(lv.info.data, "flags"), "fragFoes").items()) {
    std::string n = F.value(k, std::string());
    if (n.empty()) { const json& T = reg().mobs.is_object() && reg().mobs.contains(k) ? reg().mobs[k] : json(); n = T.is_object() ? T.value("label", k) : k; }
    if (!n.empty()) n[0] = (char)std::toupper((unsigned char)n[0]);
    out.push_back(n);
  }
  return out;
}

// ---- the story, as chapters ---------------------------------------------------------------------------------------
Story storyOf(Level& lv, Player& p) {
  const json& st = p.stats;
  const json& R = sub(lv.info.data, "rift");
  const json& E = sub(lv.info.data, "end");
  const json& S = sub(st, "story");
  const json& dims = sub(sub(st, "explore"), "dims");
  const int NEED = 4;
  int frags = std::min(NEED, R.value("frags", 0)), stage = R.value("stage", 0);
  bool sealed = R.value("sealed", false);
  int lairs = 0;
  for (auto& [k, l] : sub(lv.info.data, "dragons").items()) if (l.is_object() && l.value("slain", false)) lairs++;
  bool ancient = false;
  for (auto& [k, v] : sub(st, "sseen").items()) if (v.is_array() && v.size() > 2 && v[2].is_string() && toLower(v[2].get<std::string>()).find("ancient city") != std::string::npos) ancient = true;
  Story s;
  auto ch = [&](const char* t, std::string goal, const char* hint, bool done, bool side = false, bool epi = false, float prog = -1) { s.ch.push_back({t, goal, hint, done, side, epi, prog}); };
  ch("A Village at the Edge", "Find a village and walk into its square.", "Your journey starts beside one. Talk to the villagers, trade with them, and look at the board by the well.", keysOf(st, "vseen") > 0);
  ch("Iron and Fire", "Make yourself an iron pickaxe or better, and some armour.", "Iron ore is common below about y 60. Smelt it in a furnace. Copper and tin make bronze on the way.", S.value("iron", 0) != 0);
  ch("The Tear in the Square", "A while after you arrive, something tears open in the village square. Speak to the elder.", "Keep playing: it opens on its own a quarter of an hour in.", stage >= 1);
  ch("The Keepers", "Win four Realm Fragments (" + std::to_string(frags) + " of " + std::to_string(NEED) + ").", "Every dungeon keeper carries one (the quest board points to the nearest dungeon). So does the first of each great foe: the Cinder Golem (an Ember Effigy at night), the Hollow Archmage and the Minotaur in their towers and mazes, each of the dragons, the Warden, the Ender Dragon.", frags >= NEED || stage >= 3, false, false, frags / (float)NEED);
  ch("Fire Below", "Reach the Ember Depths.", "Beat the Cinder Golem for its core, make an Ember Igniter, and light an obsidian frame (2 wide, 3 tall inside). Or dig down far enough to break through.", dims.contains("1"), true);
  ch("Above the Clouds", "Reach the Skylands.", "Light a glowstone frame with water, or fly up on a mount. Watch for the storm dragon.", dims.contains("2"), true);
  ch("The Gateway", "Bring four fragments to the rift and use it.", "Stand by the tear in the square with the fragments and use it.", stage >= 3);
  ch("His Realm", "Step through the gateway.", "Go together and go ready: good armour, food, potions. In co-op, everyone is drawn in.", st.value("realmSeen", false) || sealed);
  ch("The Seal", "Find Herobrine at the end of his halls and defeat him.", "Five halls. His bolts hurt; he steps through space; below half health his stare stops time.", sealed);
  ch("The End Beneath", "Slay the Ender Dragon.", "Eyes of ender (an ender pearl and cinder powder) fly toward a stronghold. Or break through the void crust at the very bottom of the Depths.", E.value("slain", 0) > 0, false, true);
  ch("The Deep Dark", "Find an Ancient City.", "Far below some lands the stone turns to slate and sculk. Tread softly.", ancient, false, true);
  ch("Dragonslayer", "Slay the seven dragons of the world (" + std::to_string(lairs) + " of 7).", "Each land has its dragon: forest, frost, sand, tide, ember (the Depths), storm (the Skylands), and one more. Their eggs hatch into drakes you can ride.", lairs >= 7, false, true, lairs / 7.f);
  for (int pass = 0; pass < 3 && s.cur < 0; pass++)
    for (int i = 0; i < (int)s.ch.size(); i++) {
      auto& c = s.ch[i];
      if (c.done) continue;
      if ((pass == 0 && !c.side && !c.epi) || (pass == 1 && c.side) || pass == 2) { s.cur = i; break; }
    }
  return s;
}

std::vector<std::pair<std::string, std::string>> journalRecords(Level& lv, Player& p) {
  const json& st = p.stats;
  const json& R = sub(lv.info.data, "rift");
  double pt = lv.info.data.value("playTime", 0.0);
  return {
    {"Villages found", std::to_string(keysOf(st, "vseen"))}, {"Dungeons found", std::to_string(keysOf(st, "dseen"))}, {"Places found", std::to_string(keysOf(st, "sseen"))},
    {"Lands seen", std::to_string(sub(sub(st, "explore"), "biomes").size())}, {"Worlds visited", std::to_string(sub(sub(st, "explore"), "dims").size())},
    {"Walked", strFormat("%.1f km", st.value("walkedKm", 0.0))}, {"Creatures beaten", std::to_string(st.value("mobsKilled", 0))}, {"Quests done", std::to_string(st.value("questsDone", 0))},
    {"Realm Fragments", std::to_string(std::min(4, R.value("frags", 0))) + " of 4"}, {"Play time", strFormat("%dh %dm", (int)(pt / 3600), (int)std::fmod(pt / 60, 60))},
  };
}

} // namespace dq
