// sim/metalwork.cpp — see metalwork.h
#include <atomic>
#include "sim/metalwork.h"
#include "sim/heat.h"
#include "core/registry.h"
#include "sim/player.h"
#include "sim/gameplay.h"
#include <random>

namespace dq {

namespace {
bool isVessel(const ItemStack& s) { return s.id == "ceramic_vessel" || s.id == "crucible"; }
json& contents(ItemStack& v) { if (!v.tag.is_object()) v.tag = json::object(); json& c = v.tag["c"]; if (!c.is_array()) c = json::array(); return c; }
json compJson(const Comp& c) { json j = json::object(); for (auto& [k, f] : c) j[k] = std::round(f * 1000) / 1000; return j; }
Comp compFromJson(const json& j) { Comp c; if (j.is_object()) for (auto& [k, v] : j.items()) if (v.is_number()) c[k] = v.get<float>(); return normComp(c); }
std::string moldKind(const ItemStack& m) { const ItemDef* d = m.def(); return d ? d->str("mold") : ""; }
float rnd01() { static thread_local std::mt19937 g{std::random_device{}()}; return std::uniform_real_distribution<float>(0, 1)(g); }
}

bool vesselAdd(ItemStack& v, ItemStack& from) {
  if (!isVessel(v) || from.empty() || unitsOf(from.id) <= 0) return false;   // (onto metal already in it too: it melts in)
  // iron ore, a raw bloom, pig iron: the bloomery's and the anvil's (TFC), not to be melted and cast; a wrought iron ingot melts
  if (const std::string mt = metalOfItem(from.id); (mt == "iron" || mt == "pig_iron") && from.id != "iron_ingot" && from.id != "metal_double_ingot" && from.id.find("_sheet") == std::string::npos) return false;
  if (from.id == "raw_bloom" || from.id == "refined_bloom" || from.id == "pig_iron" || from.id == "high_carbon_steel") return false;
  json& c = contents(v);
  const int slots = v.id == "crucible" ? 16 : VESSEL_SLOTS;
  bool moved = false;
  for (auto& s : c) {   // onto a stack of the same
    ItemStack in = ItemStack::fromJson(s);
    if (in.id != from.id || in.tag != from.tag || in.n >= VESSEL_STACK) continue;
    const int k = std::min(VESSEL_STACK - in.n, from.n); in.n += k; from.n -= k; s = in.toJson(); moved = true;
    if (from.n <= 0) { from = ItemStack(); return true; }
  }
  while (!from.empty() && (int)c.size() < slots) {
    ItemStack in = from; in.n = std::min(VESSEL_STACK, from.n); from.n -= in.n;
    c.push_back(in.toJson()); moved = true;
    if (from.n <= 0) from = ItemStack();
  }
  return moved;
}
std::vector<ItemStack> vesselEmpty(ItemStack& v) {
  std::vector<ItemStack> out;
  if (!v.tag.is_object() || !v.tag.contains("c")) return out;   // the ore not yet melted (metal in it stays)
  for (auto& s : v.tag["c"]) { ItemStack i = ItemStack::fromJson(s); if (!i.empty()) out.push_back(i); }
  v.tag.erase("c"); if (v.tag.empty()) v.tag = json();
  return out;
}
int vesselUnits(const ItemStack& v) {
  if (!v.tag.is_object()) return 0;
  int u = v.tag.contains("metal") ? v.tag["metal"].value("u", 0) : 0;
  if (v.tag.contains("c")) for (auto& s : v.tag["c"]) { const ItemStack i = ItemStack::fromJson(s); u += unitsOf(i.id) * i.n; }
  return u;
}
Comp vesselComp(const ItemStack& v) {
  if (!v.tag.is_object()) return {};
  Comp c;
  if (v.tag.contains("metal")) { const float u = (float)v.tag["metal"].value("u", 0); for (auto& [m, f] : compFromJson(v.tag["metal"]["comp"])) c[m] += f * u; }
  if (v.tag.contains("c")) for (auto& s : v.tag["c"]) {
    const ItemStack i = ItemStack::fromJson(s);
    const float u = (float)unitsOf(i.id) * i.n;
    for (auto& [m, f] : compOfStack(i)) c[m] += f * u;
  }
  return normComp(c);
}
bool vesselMolten(const ItemStack& v) {
  if (!v.tag.is_object() || !v.tag.contains("metal") || vesselUnits(v) <= 0) return false;
  if (v.tag.contains("c") && !v.tag["c"].empty()) return false;   // ore still to melt in
  return itemHeat(v) >= blendMetal(vesselComp(v)).melt;
}
void vesselMeltStep(ItemStack& v) {
  if (!isVessel(v) || !v.tag.is_object() || !v.tag.contains("c") || v.tag["c"].empty()) return;
  const Comp c = vesselComp(v);
  if (c.empty() || itemHeat(v) < blendMetal(c).melt) return;
  const int u = vesselUnits(v);
  v.tag.erase("c");
  v.tag["metal"] = json{{"comp", compJson(c)}, {"u", u}};
}
bool moldFilled(const ItemStack& m) { return !moldKind(m).empty() && m.tag.is_object() && m.tag.contains("metal"); }   // (a vessel holds metal too: not a mold)

bool pourInto(ItemStack& v, ItemStack& mold, std::string* why) {
  const std::string kind = moldKind(mold);
  if (kind.empty() || moldFilled(mold)) { if (why) *why = "That isn't an empty fired mold"; return false; }
  if (!vesselMolten(v)) { if (why) *why = "The metal has to be molten to pour"; return false; }
  const int u = vesselUnits(v);
  if (u < MOLD_UNITS) { if (why) *why = "Not enough metal for that mold"; return false; }
  const Comp c = vesselComp(v);
  if (kind != "ingot" && !blendMetal(c).cast) { if (why) *why = compName(c) + " won't cast in a tool mold: forge it on an anvil"; return false; }
  mold.n = 1;
  if (!mold.tag.is_object()) mold.tag = json::object();
  mold.tag["metal"] = json{{"comp", compJson(c)}, {"u", MOLD_UNITS}};
  static std::atomic<u64> pours{std::random_device{}() ^ ((u64)std::random_device{}() << 32)};   // each pour its own (random per run): filled molds don't stack
  mold.tag["pour"] = (long long)((++pours * 0x9E3779B97F4A7C15ull) >> 2);
  setItemHeat(mold, itemHeat(v));
  if (u - MOLD_UNITS <= 0) v.tag.erase("metal"); else v.tag["metal"]["u"] = u - MOLD_UNITS;
  return true;
}

std::pair<ItemStack, bool> breakMold(ItemStack& mold, ItemStack* opened) {
  if (!moldFilled(mold)) return {ItemStack(), true};
  if (mold.n > 1) {   // stacked filled molds: open one, the rest stay filled (not all emptied, or all lost to a crack)
    ItemStack one = mold; one.n = 1;
    auto res = breakMold(one, nullptr);
    if (!res.first.empty()) { mold.n -= 1; if (opened) *opened = one; }
    return res;
  }
  const Comp c = compFromJson(mold.tag["metal"]["comp"]);
  if (itemHeat(mold) >= blendMetal(c).melt * 0.5f) return {ItemStack(), true};   // still too hot to come out
  const std::string kind = moldKind(mold);
  ItemStack piece;
  if (kind == "ingot") {
    piece = ItemStack(ingotFor(c), 1);
    if (piece.id == "alloy_ingot") piece.tag = json{{"comp", compJson(c)}};
  } else {
    piece = ItemStack("metal_" + kind + "_head", 1);
    piece.tag = json{{"comp", compJson(c)}};
  }
  setItemHeat(piece, itemHeat(mold));
  const bool survives = rnd01() >= 0.1f;
  if (survives) { mold.tag.erase("metal"); mold.tag.erase("pour"); setItemHeat(mold, itemHeat(mold)); if (mold.tag.is_object() && mold.tag.empty()) mold.tag = json(); }
  else mold = ItemStack();
  return {piece, survives};
}

bool metalworkUse(Level& lv, Player& p) {
  auto toastMsg = [&](const std::string& m, const std::string& kind = "") { toast(lv, p, m, kind); };
  ItemStack& held = p.held();
  ItemStack& off = p.offhand[0];
  if (isVessel(held)) {
    if (p.sneaking && !vesselMolten(held)) {   // empty it
      for (ItemStack& s : vesselEmpty(held)) if (int left = giveItem(lv, p, s); left > 0) { s.n = left; dropItem(lv, p.realm, s, p.pos + vec3(0, 1, 0), true); }
      return true;
    }
    if (!off.empty() && unitsOf(off.id) > 0) {
      if (!vesselAdd(held, off)) toastMsg("The vessel is full");
      return true;
    }
    if (!off.empty() && off.def() && !off.def()->str("mold").empty()) {
      ItemStack one = off; one.n = 1; std::string why;
      if (!pourInto(held, one, &why)) { toastMsg(why); return true; }
      if (--off.n <= 0) off = ItemStack();
      if (int left = giveItem(lv, p, one); left > 0) dropItem(lv, p.realm, one, p.pos + vec3(0, 1, 0), true);
      lv.fx->sound("lava_pop", p.pos + vec3(0, 1, 0), 12);
      return true;
    }
    const int u = vesselUnits(held);
    if (u > 0) toastMsg(std::to_string(u) + " units of " + compName(vesselComp(held)) + (vesselMolten(held) ? ", molten" : held.tag.contains("metal") ? ", set solid: heat it to pour" : ""));
    else toastMsg("Hold ore in your other hand and use the vessel to fill it");
    return true;
  }
  if (moldFilled(held)) {
    ItemStack opened;
    auto [piece, kept] = breakMold(held, &opened);
    if (piece.empty()) { toastMsg("Still too hot: let it cool"); return true; }
    if (!opened.empty()) if (int left = giveItem(lv, p, opened); left > 0) { opened.n = left; dropItem(lv, p.realm, opened, p.pos + vec3(0, 1, 0), true); }
    if (!kept) toastMsg("The mold cracked", "warn");
    if (int left = giveItem(lv, p, piece); left > 0) { piece.n = left; dropItem(lv, p.realm, piece, p.pos + vec3(0, 1, 0), true); }
    return true;
  }
  return false;
}

} // namespace dq
