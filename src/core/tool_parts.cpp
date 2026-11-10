// core/tool_parts.cpp — see tool_parts.h.
#include "core/tool_parts.h"
#include "common/fs.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <map>

namespace dq {

namespace {
struct Haft { const char* item; const char* adj; float speed, dur; const char* element; };
const Haft HAFTS[] = {{"stick", "", 1.f, 1.f, ""}, {"bone", "Bone-Hafted ", 0.95f, 1.3f, ""}, {"cinder_rod", "Cinder-Hafted ", 1.15f, 0.85f, "fire"},
                      {"steel_ingot", "Steel-Hafted ", 0.9f, 1.6f, ""}};
const Haft* haftOf(const std::string& item) { for (auto& h : HAFTS) if (item == h.item) return &h; return nullptr; }
// per kind, per head section: how much it does for speed, wear, damage and harvest tier
struct Weights { std::vector<float> speed, dur, dmg, tier; };
const Weights* weightsOf(const std::string& kind) {
  static const Weights P{{0.4f, 0.2f, 0.4f}, {0.25f, 0.5f, 0.25f}, {0.3f, 0.4f, 0.3f}, {0.35f, 0.3f, 0.35f}};   // pickaxe: L, M, R (tips dig)
  static const Weights A{{0.4f, 0.2f, 0.4f}, {0.3f, 0.4f, 0.3f}, {0.4f, 0.2f, 0.4f}, {0.35f, 0.3f, 0.35f}};      // axe: edge top, back, edge low
  static const Weights S{{0.5f, 0.5f}, {0.4f, 0.6f}, {0.6f, 0.4f}, {0.5f, 0.5f}};                                 // sword: tip, base
  static const Weights H{{0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}};                                 // hoe
  static const Weights V{{1.f}, {1.f}, {1.f}, {1.f}};                                                               // shovel
  return kind == "pickaxe" ? &P : kind == "axe" ? &A : kind == "sword" ? &S : kind == "hoe" ? &H : kind == "shovel" ? &V : nullptr;
}
struct Gem { const char* item; const char* name; const char* effect; float speed, dur, dmg; int tier; const char* element; };
const Gem GEMS[] = {
  {"diamond", "Diamond", "+30% durability", 1.f, 1.3f, 0.f, 0, ""},
  {"emerald", "Emerald", "+15% mining speed, +15% durability", 1.15f, 1.15f, 0.f, 0, ""},
  {"amethyst_shard", "Amethyst", "+20% mining speed", 1.2f, 1.f, 0.f, 0, ""},
  {"ruby", "Ruby", "+1.5 attack, fire", 1.f, 1.f, 3.f, 0, "fire"},
  {"sapphire", "Sapphire", "+25% durability, frost", 1.f, 1.25f, 0.f, 0, "frost"},
  {"quartz", "Quartz", "+10% mining speed, +0.5 attack", 1.1f, 1.f, 1.f, 0, ""},
  {"red_crystal", "Red Crystal", "+1 attack, +10% mining speed", 1.1f, 1.f, 2.f, 0, ""},
  {"charged_crystal", "Charged Crystal", "+25% mining speed", 1.25f, 1.f, 0.f, 0, ""},
  {"cinderite_shard", "Cinderite", "+1 harvest tier", 1.f, 1.f, 0.f, 1, "fire"},
};
const Gem* gemOf(const std::string& item) { for (auto& g : GEMS) if (item == g.item) return &g; return nullptr; }
std::string cap(std::string s) { if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]); return s; }
} // namespace

const std::vector<ToolMaterial>& toolMaterials() {
  static std::vector<ToolMaterial> v;
  static std::once_flag once;
  std::call_once(once, [] {
    auto j = readJson(dataDir() / "tool_materials.json");
    if (!j || !(*j)["materials"].is_object()) return;
    for (auto& [k, m] : (*j)["materials"].items()) {
      ToolMaterial t; t.key = k; t.name = m.value("name", k); t.prefix = m.value("prefix", k); t.element = m.value("element", std::string());
      if (m["items"].is_array()) for (auto& i : m["items"]) t.items.push_back(i.get<std::string>());
      if (m["rgb"].is_array() && m["rgb"].size() >= 3) t.rgb = vec3(m["rgb"][0].get<float>(), m["rgb"][1].get<float>(), m["rgb"][2].get<float>()) / 255.f;
      if (m.contains("stats") && m["stats"].is_object()) {
        const json& S = m["stats"];
        t.stats = true; t.tools = S.value("tools", true);
        t.sTier = S.value("tier", 2); t.sSpeed = S.value("speed", 6.f); t.sDamage = S.value("damage", 3.f); t.sDur = S.value("durability", 200.f);
        t.armorK = S.value("armor", 0.f); t.armorDurK = S.value("armorDur", 0.f); t.fireResist = S.value("fireResist", 0.f);
      }
      v.push_back(std::move(t));
    }
  });
  return v;
}
const ToolMaterial* toolMaterial(const std::string& key) { for (auto& m : toolMaterials()) if (m.key == key) return &m; return nullptr; }
const ToolMaterial* toolMaterialOfItem(const std::string& item) {
  for (auto& m : toolMaterials()) for (auto& want : m.items) {
    if (want == item) return &m;
    if (!want.empty() && want[0] == '#') { auto it = reg().tags.find(want); if (it != reg().tags.end() && std::find(it->second.begin(), it->second.end(), item) != it->second.end()) return &m; }
  }
  return nullptr;
}

bool isPartTool(const ItemStack& s) { return !s.empty() && s.tag.is_object() && s.tag.contains("pt") && s.tag["pt"].is_object(); }
bool isPartArmor(const ItemStack& s) { return !s.empty() && s.tag.is_object() && s.tag.contains("pa") && s.tag["pa"].is_object(); }

const std::vector<ArmorPattern>& armorPatterns() {
  static const std::vector<ArmorPattern> P = {
    {"helmet", 0, 3, 2, {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {2, 1}}},
    {"chestplate", 1, 3, 3, {{0, 0}, {2, 0}, {0, 1}, {1, 1}, {2, 1}, {0, 2}, {1, 2}, {2, 2}}},
    {"leggings", 2, 3, 3, {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {2, 1}, {0, 2}, {2, 2}}},
    {"boots", 3, 3, 2, {{0, 0}, {2, 0}, {0, 1}, {2, 1}}},
  };
  return P;
}
const ArmorPattern* armorPattern(const std::string& piece) { for (auto& p : armorPatterns()) if (p.piece == piece) return &p; return nullptr; }

bool makePartArmor(const std::string& piece, const std::vector<std::string>& mats, ItemStack& out) {
  const ArmorPattern* P = armorPattern(piece);
  if (!P || mats.size() != P->cells.size() || !reg().item("mixed_" + piece)) return false;
  float pts = 0, dur = 0, fr = 0; std::vector<std::string> distinct;
  const ItemDef* iron = reg().item("iron_" + piece);
  for (auto& k : mats) {   // each cell gives its share: more of a material, more of what it is
    const ToolMaterial* m = toolMaterial(k);
    const ItemDef* a = m ? reg().item(m->prefix + "_" + piece) : nullptr;
    if (a && a->props.contains("armor") && a->props["armor"].is_object()) {
      const json& A = a->props["armor"];
      pts += A.value("points", 0.f); dur += A.value("durability", 0.f); fr = std::max(fr, A.value("fireResist", 0.f));
    } else if (m && m->armorK > 0 && iron && iron->props.contains("armor") && iron->props["armor"].is_object()) {   // no piece of its own: iron's, scaled
      const json& A = iron->props["armor"];
      pts += A.value("points", 0.f) * m->armorK; dur += A.value("durability", 0.f) * (m->armorDurK > 0 ? m->armorDurK : m->armorK); fr = std::max(fr, m->fireResist);
    } else return false;
    if (std::find(distinct.begin(), distinct.end(), k) == distinct.end()) distinct.push_back(k);
  }
  const float n = (float)mats.size();
  out = ItemStack("mixed_" + piece, 1);
  out.tag = json{{"pa", {{"p", piece}, {"m", mats}, {"pts", std::round(pts / n * 10) / 10 + 0.25f * (float)(distinct.size() - 1)},
                         {"dur", (int)std::lround(dur / n * (1 + 0.06f * (float)(distinct.size() - 1)))}, {"fr", fr}}}};
  return true;
}
float armorPointsOf(const ItemStack& s) {
  if (isPartArmor(s)) return s.tag["pa"].value("pts", 0.f);
  const ItemDef* d = s.empty() ? nullptr : s.def();
  return d && d->props.contains("armor") && d->props["armor"].is_object() ? d->props["armor"].value("points", 0.f) : 0.f;
}
float armorFireResistOf(const ItemStack& s) {
  if (isPartArmor(s)) return s.tag["pa"].value("fr", 0.f);
  const ItemDef* d = s.empty() ? nullptr : s.def();
  return d && d->props.contains("armor") && d->props["armor"].is_object() ? d->props["armor"].value("fireResist", 0.f) : 0.f;
}
// worn, a mixed piece is drawn with each crafting cell's material over the part of the body it lies on (assets/char_model.cpp
// armorMeshes reads the "pa:<piece>:<m1,...>" key)
std::string partArmorWornLook(const ItemStack& s) { return isPartArmor(s) ? partToolModelKey(s) : s.id; }

ToolInfo toolOf(const ItemStack& s) {
  const ItemDef* d = s.empty() ? nullptr : s.def();
  ToolInfo t = d && d->hasTool ? d->tool : ToolInfo{};
  if (s.empty() || !s.tag.is_object()) return t;
  if (isPartTool(s)) {
    const json& p = s.tag["pt"];
    t.tier = p.value("tier", t.tier); t.speed = p.value("speed", t.speed); t.damage = p.value("dmg", t.damage); t.durability = p.value("dur", t.durability);
  }
  if (s.tag.contains("gem") && s.tag["gem"].is_string()) if (const Gem* g = gemOf(s.tag["gem"].get<std::string>())) {
    t.speed *= g->speed; t.durability = (int)std::lround(t.durability * g->dur); t.damage += g->dmg; t.tier += g->tier;
  }
  return t;
}

int toolMaxDurability(const ItemStack& s) {
  const ItemDef* d = s.empty() ? nullptr : s.def();
  if (!d) return 0;
  if (d->hasTool) return std::max(0, toolOf(s).durability);
  if (isPartArmor(s)) return s.tag["pa"].value("dur", 0);
  if (d->props.contains("armor") && d->props["armor"].is_object()) return d->props["armor"].value("durability", 0);
  return 0;
}

std::string stackName(const ItemStack& s) {
  const ItemDef* d = s.empty() ? nullptr : s.def();
  std::string base = d ? d->name : s.id;
  if (s.tag.is_object() && s.tag.contains("name") && s.tag["name"].is_string() && !isPartTool(s)) {   // a written label, a filled steel bucket
    const std::string n = s.tag["name"].get<std::string>();
    return d && d->has("label") ? base + " \xC2\xB7 \xE2\x80\x9C" + n + "\xE2\x80\x9D" : n;
  }
  if (isPartTool(s)) {
    const json& p = s.tag["pt"];
    std::vector<std::string> names;
    if (p["m"].is_array()) for (auto& m : p["m"]) if (m.is_string()) { const ToolMaterial* tm = toolMaterial(m.get<std::string>()); std::string n = tm ? tm->name : m.get<std::string>(); if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n); }
    std::string mats;
    for (size_t i = 0; i < names.size(); i++) mats += (i ? (i + 1 == names.size() ? " & " : ", ") : "") + names[i];
    const Haft* h = haftOf(p.value("h", std::string("stick")));
    base = std::string(h ? h->adj : "") + mats + " " + cap(p.value("t", std::string("tool")));
  } else if (isPartArmor(s)) {
    std::vector<std::string> names;
    for (auto& m : s.tag["pa"]["m"]) if (m.is_string()) { const ToolMaterial* tm = toolMaterial(m.get<std::string>()); std::string n = tm ? tm->name : m.get<std::string>(); if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n); }
    std::string mats;
    for (size_t i = 0; i < names.size(); i++) mats += (i ? (i + 1 == names.size() ? " & " : ", ") : "") + names[i];
    base = mats + " " + cap(s.tag["pa"].value("p", std::string("armor")));
  }
  if (s.tag.is_object() && s.tag.contains("gem") && s.tag["gem"].is_string()) if (const Gem* g = gemOf(s.tag["gem"].get<std::string>())) base += std::string(" (") + g->name + ")";
  return base;
}

std::string partToolModelKey(const ItemStack& s) {
  if (isPartArmor(s)) {   // "pa:<piece>:<m1,m2,...>" (one per cell)
    std::string k = "pa:" + s.tag["pa"].value("p", std::string("helmet")) + ":";
    const json& m = s.tag["pa"]["m"];
    for (size_t i = 0; i < m.size(); i++) k += (i ? "," : "") + m[i].get<std::string>();
    return k;
  }
  const bool pt = isPartTool(s), gem = !s.empty() && s.tag.is_object() && s.tag.contains("gem") && s.tag["gem"].is_string();
  if (!pt && !gem) return "";
  std::string k = "pt:";
  if (pt) {
    const json& p = s.tag["pt"];
    k += p.value("t", std::string("pickaxe")) + ":";
    if (p["m"].is_array()) for (size_t i = 0; i < p["m"].size(); i++) k += (i ? "," : "") + p["m"][i].get<std::string>();
    k += ":" + p.value("h", std::string("stick"));
  } else k += "=" + s.id + "::";   // a plain tool with a gem set in it
  k += ":" + (gem ? s.tag["gem"].get<std::string>() : std::string());
  return k;
}

const std::vector<ToolPattern>& toolPatterns() {
  static const std::vector<ToolPattern> P = {
    {"pickaxe", 3, 3, {{0, 0}, {1, 0}, {2, 0}}, {{1, 1}, {1, 2}}},
    {"axe", 2, 3, {{0, 0}, {1, 0}, {0, 1}}, {{1, 1}, {1, 2}}},
    {"shovel", 1, 3, {{0, 0}}, {{0, 1}, {0, 2}}},
    {"sword", 1, 3, {{0, 0}, {0, 1}}, {{0, 2}}},
    {"hoe", 2, 3, {{0, 0}, {1, 0}}, {{1, 1}, {1, 2}}},
  };
  return P;
}

bool makePartTool(const std::string& kind, const std::vector<std::string>& mats, const std::string& haft, ItemStack& out) {
  const Weights* W = weightsOf(kind);
  const Haft* H = haftOf(haft);
  if (!W || !H || mats.empty() || mats.size() != W->speed.size() || !reg().item("mixed_" + kind)) return false;
  float sp = 0, du = 0, dm = 0, ti = 0; std::string el = H->element;
  std::vector<std::string> distinct;
  const ItemDef* base = reg().item("iron_" + kind), *ironPick = reg().item("iron_pickaxe");
  for (size_t i = 0; i < mats.size(); i++) {   // each head section gives its share, weighted by the work that part does
    const ToolMaterial* m = toolMaterial(mats[i]);
    if (!m || !m->tools) return false;
    const ItemDef* t = reg().item(m->prefix + "_" + kind);
    if (!t || !t->hasTool) t = reg().item(m->prefix + "_pickaxe");   // a material with no tool of this kind: its pickaxe's
    float tsp, tdu, tdm, tti;
    if (t && t->hasTool) {
      const float dmgK = base && base->hasTool ? base->tool.damage / 4.f : 1.f;   // (a pickaxe's damage, brought to this kind's)
      tdm = t->key == m->prefix + "_" + kind ? t->tool.damage : t->tool.damage * dmgK;
      tsp = t->tool.speed; tdu = (float)t->tool.durability; tti = (float)t->tool.tier;
    } else if (m->stats && base && base->hasTool && ironPick && ironPick->hasTool) {   // no tools of its own: its stats, as iron's are for this kind
      tsp = base->tool.speed * m->sSpeed / std::max(0.1f, ironPick->tool.speed);
      tdu = m->sDur * (float)base->tool.durability / (float)std::max(1, ironPick->tool.durability);
      tdm = base->tool.damage + (m->sDamage - ironPick->tool.damage);
      tti = (float)m->sTier;
    } else return false;
    sp += W->speed[i] * tsp; du += W->dur[i] * tdu; dm += W->dmg[i] * tdm; ti += W->tier[i] * tti;
    if (el.empty()) el = m->element;
    if (std::find(distinct.begin(), distinct.end(), mats[i]) == distinct.end()) distinct.push_back(mats[i]);
  }
  du *= 1 + 0.08f * (float)(distinct.size() - 1);   // a head of several metals worked together is a little tougher
  out = ItemStack("mixed_" + kind, 1);
  json p = {{"t", kind}, {"m", mats}, {"h", haft}, {"tier", (int)std::lround(ti)}, {"speed", std::round(sp * H->speed * 100) / 100},
            {"dmg", std::round(dm * 10) / 10}, {"dur", (int)std::lround(du * H->dur)}};
  if (!el.empty()) p["el"] = el;
  out.tag = json{{"pt", p}};
  return true;
}

const char* gemEffect(const std::string& gemItem) { const Gem* g = gemOf(gemItem); return g ? g->effect : nullptr; }
bool setToolGem(ItemStack& tool, const std::string& gemItem) {
  const ItemDef* d = tool.empty() ? nullptr : tool.def();
  if (!d || !d->hasTool || !gemOf(gemItem)) return false;
  static const char* KINDS[] = {"pickaxe", "axe", "shovel", "sword", "hoe"};
  bool kindOk = false; for (const char* k : KINDS) if (d->tool.type == k) kindOk = true;
  if (!kindOk) return false;
  if (tool.tag.is_object() && tool.tag.contains("gem")) return false;
  if (!tool.tag.is_object()) tool.tag = json::object();
  tool.tag["gem"] = gemItem;
  return true;
}

} // namespace dq
