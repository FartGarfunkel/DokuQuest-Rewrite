// sim/anvil.cpp — see anvil.h
#include "sim/anvil.h"
#include "sim/heat.h"
#include "core/tool_parts.h"
#include "common/fs.h"
#include <mutex>

namespace dq {

int anvilMoveValue(AnvilMove m) { static const int V[] = {-3, -6, -9, -15, 2, 7, 13, 16}; return V[(int)m]; }
AnvilStep anvilStepOf(AnvilMove m) {
  switch (m) { case AnvilMove::Draw: return AnvilStep::Draw; case AnvilMove::Punch: return AnvilStep::Punch; case AnvilMove::Bend: return AnvilStep::Bend;
    case AnvilMove::Upset: return AnvilStep::Upset; case AnvilMove::Shrink: return AnvilStep::Shrink; default: return AnvilStep::Hit; }
}
const char* anvilMoveName(AnvilMove m) { static const char* N[] = {"Light Hit", "Medium Hit", "Heavy Hit", "Draw", "Punch", "Bend", "Upset", "Shrink"}; return N[(int)m]; }

const std::vector<AnvilRecipe>& anvilRecipes() {
  static std::vector<AnvilRecipe> v; static std::once_flag once;
  std::call_once(once, [] {
    auto j = readJson(dataDir() / "anvil.json");
    if (!j || !(*j)["recipes"].is_array()) return;
    static const std::map<std::string, AnvilStep> S = {{"hit", AnvilStep::Hit}, {"draw", AnvilStep::Draw}, {"punch", AnvilStep::Punch}, {"bend", AnvilStep::Bend}, {"upset", AnvilStep::Upset}, {"shrink", AnvilStep::Shrink}};
    static const std::map<std::string, AnvilRule::Where> W = {{"last", AnvilRule::Last}, {"second", AnvilRule::Second}, {"third", AnvilRule::Third}, {"notlast", AnvilRule::NotLast}, {"any", AnvilRule::Any}};
    for (auto& r : (*j)["recipes"]) {
      AnvilRecipe a; a.key = r.value("key", ""); a.name = r.value("name", a.key); a.in = r.value("in", "ingot"); a.out = r.value("out", ""); a.n = r.value("n", 1); a.weld = r.value("weld", false);
      for (auto& p : r["plan"]) {
        const std::string s = p.get<std::string>(); const size_t c = s.find(':');
        auto st = S.find(s.substr(0, c)); auto wh = W.find(c == std::string::npos ? "any" : s.substr(c + 1));
        if (st != S.end() && wh != W.end()) a.plan.push_back({st->second, wh->second});
      }
      v.push_back(a);
    }
  });
  return v;
}
const AnvilRecipe* anvilRecipe(const std::string& k) { for (auto& r : anvilRecipes()) if (r.key == k) return &r; return nullptr; }

std::string anvilPieceOf(const ItemStack& s) {
  if (s.empty()) return "";
  if (s.id == "metal_double_ingot") return "double_ingot";
  if (s.id == "metal_sheet") return "sheet";
  if (s.id == "metal_double_sheet") return "double_sheet";
  if (s.id == "raw_bloom") return "bloom";
  if (s.id == "pig_iron") return "pig_iron";
  if (s.id == "refined_bloom") return "refined_bloom";
  if (s.id == "high_carbon_steel") return "high_carbon_steel";
  if (unitsOf(s.id) == 100 && !metalOfItem(s.id).empty()) return "ingot";
  if (s.id == "alloy_ingot") return "ingot";
  return "";
}
int anvilTarget(u64 seed, const std::string& r) {
  u64 h = seed * 0x9E3779B97F4A7C15ull;
  for (char c : r) { h ^= (u8)c; h *= 0x100000001B3ull; }
  h ^= h >> 29; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 32;
  return 70 + (int)(h % 50);
}
void anvilApply(AnvilWork& w, AnvilMove m) {
  w.value = std::clamp(w.value + anvilMoveValue(m), 0, 150);
  w.steps.push_back(anvilStepOf(m));
  if (w.steps.size() > 3) w.steps.erase(w.steps.begin());
}
bool anvilPlanMet(const AnvilRecipe& r, const std::vector<AnvilStep>& st) {
  const int n = (int)st.size();
  auto at = [&](int back) -> int { return n - 1 - back >= 0 ? (int)st[n - 1 - back] : -1; };   // 0 = last
  for (const AnvilRule& rule : r.plan) {
    const int want = (int)rule.step;
    bool ok = false;
    switch (rule.where) {
      case AnvilRule::Last: ok = at(0) == want; break;
      case AnvilRule::Second: ok = at(1) == want; break;
      case AnvilRule::Third: ok = at(2) == want; break;
      case AnvilRule::NotLast: ok = at(1) == want || at(2) == want; break;
      case AnvilRule::Any: ok = at(0) == want || at(1) == want || at(2) == want; break;
    }
    if (!ok) return false;
  }
  return true;
}
bool anvilDone(const AnvilWork& w) {
  const AnvilRecipe* r = anvilRecipe(w.recipe);
  return r && std::abs(w.value - w.target) <= 3 && anvilPlanMet(*r, w.steps);
}
float anvilQuality(const AnvilWork& w) { const int d = std::abs(w.value - w.target); return d == 0 ? 1.15f : 1.f - 0.05f * d; }

bool anvilCanWork(const ItemStack& p, const AnvilRecipe& r, int tier, std::string* why) {
  if (anvilPieceOf(p) != r.in) { if (why) *why = "That needs a " + r.in; return false; }
  if (p.n < r.n) { if (why) *why = "That needs " + std::to_string(r.n) + " of them"; return false; }
  const Comp c = compOfStack(p);
  const Metal m = blendMetal(c.empty() ? Comp{{"iron", 1.f}} : c);
  if (m.anvil > tier + 1) {   // an anvil works its own tier and the next (TFC): stone → copper → bronze → iron → steel...
    if (why) *why = m.name + " needs a better anvil"; return false; }
  const float h = itemHeat(p), lo = m.melt * (r.weld ? 0.8f : 0.6f);
  if (h < lo) { if (why) *why = "Too cold to work: heat it to " + std::to_string((int)lo) + "\xC2\xB0" "C"; return false; }
  if (h >= m.melt) { if (why) *why = "It's molten!"; return false; }
  return true;
}

std::string anvilBlockFor(const Comp& c) {
  const int t = blendMetal(c).anvil;
  return t >= 6 ? "adamantite_anvil" : t >= 5 ? "mithril_anvil" : t >= 4 ? "steel_anvil" : t >= 3 ? "anvil" : t >= 2 ? "bronze_anvil" : "copper_anvil";
}
int anvilTierOfBlock(const std::string& k) {
  static const std::map<std::string, int> T = {{"stone_anvil", 0}, {"copper_anvil", 1}, {"bronze_anvil", 2}, {"anvil", 3}, {"chipped_anvil", 3}, {"damaged_anvil", 3},
                                                {"steel_anvil", 4}, {"mithril_anvil", 5}, {"adamantite_anvil", 6}};
  auto it = T.find(k); return it == T.end() ? -1 : it->second;
}

ItemStack anvilOutput(const AnvilRecipe& r, const ItemStack& piece, float quality) {
  const Comp c = compOfStack(piece);
  json jc = json::object(); for (auto& [k, v] : c) jc[k] = std::round(v * 1000) / 1000;
  ItemStack out;
  if (r.out.rfind("metal_", 0) == 0) { out = ItemStack(r.out, 1); out.tag = json{{"comp", jc}}; if (r.out.find("_head") != std::string::npos) out.tag["ql"] = std::round(quality * 100) / 100; }
  else if (r.out.rfind("armor:", 0) == 0) {
    const std::string pieceName = r.out.substr(6);
    const ToolMaterial* tm = toolMaterialOfStack(piece);
    const ArmorPattern* P = nullptr; for (auto& ap : armorPatterns()) if (ap.piece == pieceName) P = &ap;
    if (tm && P) {
      makePartArmor(pieceName, std::vector<std::string>(P->cells.size(), tm->key), out);
      if (out.tag.is_object() && out.tag.contains("pa")) out.tag["pa"]["dur"] = (int)std::lround(out.tag["pa"].value("dur", 0) * quality);
      if (!out.empty() && reg().item("unfinished_" + pieceName)) out.id = "unfinished_" + pieceName;   // padded at the crafting table to finish (TFC)
    }
  } else if (r.out == "anvil") out = ItemStack(anvilBlockFor(c), 1);
  else out = ItemStack(r.out, 1);
  if (!out.empty()) setItemHeat(out, itemHeat(piece));
  return out;
}

} // namespace dq
