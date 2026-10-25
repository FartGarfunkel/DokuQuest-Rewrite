#include "sim/magic_data.h"
#include <random>
#include <fstream>

namespace dq {

static MagicData g_md;

double mrand() { thread_local std::mt19937 g{std::random_device{}()}; return std::uniform_real_distribution<double>(0, 1)(g); }

u32 hexColor(const std::string& s) {
  if (s.size() < 7 || s[0] != '#') return 0xffffff;
  return (u32)std::strtoul(s.c_str() + 1, nullptr, 16) & 0xffffff;
}

using ojson = nlohmann::ordered_json;
static const ojson& G(const ojson& o, const char* k) { static const ojson E; return o.is_object() && o.contains(k) ? o.at(k) : E; }

static std::vector<PotionFx> fxList(const nlohmann::ordered_json& a) {
  std::vector<PotionFx> v;
  if (!a.is_array()) return v;
  for (auto& e : a) if (e.is_array() && e.size() >= 2) v.push_back({e[0].get<std::string>(), e[1].get<float>(), e.size() > 2 && e[2].is_number() ? e[2].get<int>() : 0});
  return v;
}

bool loadMagicData(const fs::path& file) {
  // ordered: the original's key order matters (enchantment picks, the effect list)
  std::ifstream in(file);
  if (!in) { DQ_WARN("magic: %s missing", file.string().c_str()); return false; }
  nlohmann::ordered_json j;
  try { j = nlohmann::ordered_json::parse(in); } catch (std::exception& e) { DQ_WARN("magic: %s: %s", file.string().c_str(), e.what()); return false; }
  MagicData d;
  for (auto& k : G(j, "spellOrder")) d.spellOrder.push_back(k);
  for (auto& [k, v] : G(j, "spells").items()) {
    SpellDef s; s.key = k; s.name = v.value("name", k); s.col = v.value("col", "#ffffff"); s.fx = v.value("fx", "mana_arcane");
    s.school = v.value("school", "arcane"); s.desc = v.value("desc", ""); s.mana = v.value("mana", 0); s.lvl = v.value("lvl", 1);
    s.util = v.value("util", 0) != 0; s.cd = v.value("cd", 0.f);
    d.spells[k] = s;
  }
  for (auto& s : G(j, "schools")) d.schools.push_back({s[0].get<std::string>(), s[1].get<std::string>(), s[2].get<std::string>()});
  for (auto& [k, v] : G(j, "ench").items()) {
    EnchDef e; e.key = k; e.name = v.value("name", k); e.group = v.value("group", ""); e.desc = v.value("desc", "");
    e.max = v.value("max", 1); e.w = v.value("w", 1); e.minP = v.value("minP", 1); e.treasure = v.value("treasure", false);
    for (auto& o : G(v, "on")) e.on.push_back(o);
    for (auto& o : G(v, "excl")) e.excl.push_back(o);
    d.enchOrder.push_back(k); d.ench[k] = e;
  }
  for (auto& [k, v] : G(j, "enchItems").items()) d.enchItems[k] = {v[0].get<std::string>(), v[1].get<int>()};
  for (auto& r : G(j, "roman")) d.roman.push_back(r);
  std::string runes = j.value("runes", "");
  for (size_t i = 0; i < runes.size();) { size_t n = (runes[i] & 0x80) == 0 ? 1 : (runes[i] & 0xE0) == 0xC0 ? 2 : (runes[i] & 0xF0) == 0xE0 ? 3 : 4; d.runes.push_back(runes.substr(i, n)); i += n; }
  for (auto& [k, v] : G(j, "effects").items()) { d.effectOrder.push_back(k); d.effects[k] = {k, v.value("name", k), v.value("col", "#ffffff"), v.value("bad", false)}; }
  for (auto& [k, v] : G(j, "potions").items()) {
    PotionDef p; p.key = k; p.name = v.value("name", k); p.col = v.value("col", "#3a6ad8"); p.ing = v.value("ing", "");
    p.fx = fxList(G(v, "fx")); p.hasLong = v.contains("long"); p.lng = fxList(G(v, "long"));
    p.hasStrong = v.contains("strong"); p.strong = fxList(G(v, "strong"));
    d.potionOrder.push_back(k); d.potions[k] = p;
  }
  for (auto& [k, v] : G(j, "brew").items()) for (auto& [ing, to] : v.items()) d.brew[k][ing] = to;
  for (auto& i : G(j, "brewIngs")) d.brewIngs.insert(i.get<std::string>());
  d.brewTime = j.value("brewTime", 12.f);
  for (auto& s : G(j, "skills")) d.skills.push_back({s[0], s[1], s[2]});
  for (auto& x : G(j, "skillXp")) d.skillXp.push_back(x.get<double>());
  for (auto& [k, v] : G(j, "perks").items()) for (auto& p : v) d.perks[k].push_back({p[0].get<int>(), p[1].get<std::string>(), p[2].get<std::string>()});
  for (auto& [k, v] : G(j, "oreXp").items()) d.oreXp[k] = v;
  for (auto& [k, v] : G(j, "alch").items()) d.alch[k] = v;
  auto plain = [&](const char* k) { return j.contains(k) ? json::parse(j[k].dump()) : json(); };
  d.robes = plain("robes"); d.paths = plain("paths"); d.fragFoes = plain("fragFoes"); d.dimLabel = plain("dimLabel");
  d.spiritWolf = plain("spiritWolf"); d.pathAt = j.value("pathAt", 20);
  d.loaded = true;
  g_md = std::move(d);
  return true;
}

const MagicData& magicData() {
  if (!g_md.loaded) loadMagicData(dataDir() / "magic.json");
  return g_md;
}

} // namespace dq
