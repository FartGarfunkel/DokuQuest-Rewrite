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

bool metalworkPourOnGround(Level& lv, Player& p, World& w, int x, int y, int z) {
  ItemStack& held = p.held();
  if (!isVessel(held)) return false;
  json* e = w.blockEntity(x, y, z);
  if (!e || !e->is_object() || e->value("type", "") != "pile" || !(*e)["items"].is_array()) return false;
  json& items = (*e)["items"];
  while (items.size() < 4) items.push_back(nullptr);   // (an older pile's shorter list: its quarters all there)
  if (!(*e)["rot"].is_array()) (*e)["rot"] = json::array({0, 0, 0, 0});
  while ((*e)["rot"].size() < 4) (*e)["rot"].push_back(0);
  int at = -1, free = -1;
  for (int i = 0; i < (int)items.size(); i++) {
    if (items[i].is_null()) { if (free < 0) free = i; continue; }
    const ItemStack s = ItemStack::fromJson(items[i]);
    if (at < 0 && s.def() && !s.def()->str("mold").empty() && !moldFilled(s)) at = i;
  }
  if (at < 0) return false;
  std::string why;
  if (!vesselMolten(held)) { toast(lv, p, vesselUnits(held) > 0 ? "The metal has to be molten to pour: heat the vessel on a charcoal fire or in a forge" : "The vessel is empty"); return true; }
  ItemStack stack = ItemStack::fromJson(items[at]), one = stack; one.n = 1;
  if (stack.n > 1 && free < 0) { toast(lv, p, "Set the molds down one to a spot to pour into them"); return true; }
  if (!pourInto(held, one, &why)) { toast(lv, p, why); return true; }
  if (stack.n > 1) { stack.n -= 1; items[at] = stack.toJson(); items[free] = one.toJson(); (*e)["rot"][free] = (*e)["rot"][at]; }
  else items[at] = one.toJson();
  w.blockEntityChanged(x, y, z);
  lv.fx->sound("lava_pop", vec3(x + 0.5f, y + 0.3f, z + 0.5f), 12);
  return true;
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
    // pour: into the fired mold in the other hand, else the first empty fired mold in the bag (TFC pours from the vessel's
    // screen into any mold you carry)
    auto emptyMold = [](const ItemStack& s) { return !s.empty() && s.def() && !s.def()->str("mold").empty() && !moldFilled(s); };
    ItemStack* src = emptyMold(off) ? &off : nullptr;
    if (!src && vesselMolten(held)) for (auto& s : p.inv) if (emptyMold(s)) { src = &s; break; }
    if (src) {
      ItemStack one = *src; one.n = 1; std::string why;
      if (!pourInto(held, one, &why)) { toastMsg(why); return true; }
      if (--src->n <= 0) *src = ItemStack();
      if (int left = giveItem(lv, p, one); left > 0) dropItem(lv, p.realm, one, p.pos + vec3(0, 1, 0), true);
      lv.fx->sound("lava_pop", p.pos + vec3(0, 1, 0), 12);
      toastMsg("Poured into a " + one.def()->name + ": let it cool, then use it to take the piece out");
      return true;
    }
    const int u = vesselUnits(held);
    const Comp c = vesselComp(held);
    const int melt = (int)blendMetal(c).melt, now = (int)itemHeat(held);
    if (u > 0 && vesselMolten(held)) toastMsg(std::to_string(u) + " units of molten " + compName(c) + ": carry a fired mold (or use it on one set on the ground) to pour");
    else if (u > 0) toastMsg(std::to_string(u) + " units of " + compName(c) + ": it melts at " + std::to_string(melt) + "\xC2\xB0" "C (now " + std::to_string(now) + "\xC2\xB0" "C). Heat it on a charcoal fire or in a forge");
    else toastMsg("Hold ore or metal in your other hand and use the vessel to fill it");
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
