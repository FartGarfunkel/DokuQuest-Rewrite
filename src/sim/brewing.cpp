// sim/brewing.cpp — game/brewing.js: potions, the brewing stand, drinking, throwing, bottles; Bottle o' Enchanting.
#include "sim/brewing.h"
#include "sim/effects.h"
#include "sim/magic.h"
#include "sim/magic_fx.h"
#include "sim/magic_entities.h"
#include "sim/rpg.h"
#include "sim/physics.h"
#include "world/world.h"

namespace dq {

static double rnd() { return mrand(); }

std::string potKey(const std::string& P, const std::string& v, bool splash) { return (splash ? "splash_potion_" : "potion_") + P + (v.empty() ? "" : "_" + v); }
const json* potionInfo(const std::string& id) {
  const ItemDef* d = reg().item(id);
  return d && d->props.contains("potion") && d->props["potion"].is_object() ? &d->props["potion"] : nullptr;
}
std::string brewResult(const std::string& from, const std::string& ing) {
  const auto& B = magicData().brew;
  auto it = B.find(from);
  if (it == B.end()) return "";
  auto j = it->second.find(ing);
  return j == it->second.end() ? "" : j->second;
}
bool isBrewIngredient(const std::string& id) { return magicData().brewIngs.count(id) > 0; }
bool brewAccepts(int i, const std::string& id) {
  if (i < 3) return potionInfo(id) != nullptr;
  if (i == 3) return isBrewIngredient(id);
  return id == "cinder_powder";
}

json& brewEntity(World& w, int x, int y, int z) {
  json* e = w.blockEntity(x, y, z, true);
  if (!e->is_object() || e->value("type", "") != "brew") *e = json{{"type", "brew"}, {"items", json::array({nullptr, nullptr, nullptr, nullptr, nullptr})}, {"fuel", 0}, {"t", 0}};
  json& it = (*e)["items"];
  if (!it.is_array()) it = json::array();
  while (it.size() < 5) it.push_back(nullptr);
  return *e;
}

static ItemStack slot(const json& e, int i) { return ItemStack::fromJson(e["items"][i]); }
static void setSlot(json& e, int i, const ItemStack& s) { e["items"][i] = s.toJson(); }

bool tickBrew(Level& lv, World& w, json& e, double dt, int x, int y, int z) {
  ItemStack ing = slot(e, 3), fuelS = slot(e, 4);
  std::string outs[3];
  bool can = false;
  for (int i = 0; i < 3; i++) { ItemStack b = slot(e, i); outs[i] = !b.empty() && !ing.empty() ? brewResult(b.id, ing.id) : ""; can |= !outs[i].empty(); }
  can = can && !ing.empty();
  bool changed = false, busy = e.value("t", 0.0) > 0;
  vec3 c(x + 0.5f, y + 0.5f, z + 0.5f);
  if (can && !(e.value("fuel", 0) > 0) && fuelS.id == "cinder_powder") { e["fuel"] = 20; fuelS.n--; if (fuelS.n <= 0) fuelS.clear(); setSlot(e, 4, fuelS); changed = true; }
  if (can && e.value("fuel", 0) > 0) {
    if (!(e.value("t", 0.0) > 0) || e.value("ing", "") != ing.id) { e["t"] = magicData().brewTime; e["ing"] = ing.id; changed = true; }
    e["t"] = e.value("t", 0.0) - dt;
    if (rnd() < dt * 0.8) lv.fx->sound("brew_bubble", c, 10);
    if (e.value("t", 0.0) <= 0) {
      int made = 0; ItemStack sample;
      for (int i = 0; i < 3; i++) if (!outs[i].empty()) { setSlot(e, i, ItemStack(outs[i], 1)); made++; if (sample.empty()) sample = ItemStack(outs[i], 1); }
      ing.n--; if (ing.n <= 0) ing.clear(); setSlot(e, 3, ing);
      e["fuel"] = e.value("fuel", 0) - 1; e["t"] = 0; e["ing"] = nullptr; changed = true;
      lv.fx->sound("brew_done", c, 14);
      if (busy && made) onBrewDone(lv, w.realm, c, made, &sample);
    }
  } else if (e.value("t", 0.0) > 0) { e["t"] = 0; e["ing"] = nullptr; changed = true; }
  return changed;
}

std::string brewStatus(const json& e) {
  ItemStack ing = slot(e, 3);
  bool any = false, ok = false;
  for (int i = 0; i < 3; i++) { ItemStack b = slot(e, i); if (!b.empty()) { any = true; if (!ing.empty() && !brewResult(b.id, ing.id).empty()) ok = true; } }
  if (e.value("t", 0.0) > 0) return "Brewing\xE2\x80\xA6";
  if (!any) return "Bottles of water below, an ingredient on top (nether wart first)";
  if (ing.empty()) return "Add an ingredient on top";
  if (!ok) return "That does nothing to these";
  if (!(e.value("fuel", 0) > 0) && slot(e, 4).id != "cinder_powder") return "Needs cinder powder to burn";
  return "";
}
float brewProgress(const json& e) { double t = e.value("t", 0.0); return t > 0 ? (float)(1 - t / magicData().brewTime) : 0.f; }
float brewFuel(const json& e) { return std::min(1.f, e.value("fuel", 0) / 20.f); }

// ---- drinking and throwing -------------------------------------------------------------------------------
bool drinkPotion(Level& lv, Player& p, ItemStack& held) {
  const json* P = potionInfo(held.id);
  if (!P) return false;
  if (P->value("splash", false)) { throwPotion(lv, p, held); return true; }
  const ItemDef* d = held.def();
  std::string first;
  for (auto& f : (*P)["fx"]) { giveEffect(lv, p, f[0], f[1].get<float>(), f.size() > 2 && f[2].is_number() ? f[2].get<int>() : 0, &p); if (first.empty()) first = f[0]; }
  if (!p.creative()) { if (--held.n <= 0) held = ItemStack("glass_bottle", 1); else giveOrDrop(lv, p, ItemStack("glass_bottle", 1)); }
  lv.fx->sound("drink", p.pos + vec3(0, 1.4f, 0), 12);
  vec3 look(-std::sin(p.yaw) * std::cos(p.pitch), std::sin(p.pitch), -std::cos(p.yaw) * std::cos(p.pitch));
  std::string mote = magicData().effect(first) ? "fxmote_" + first : "fxmote_water";
  sfx::burst(lv, p.pos + vec3(look.x * 0.4f, 1.62f - 0.25f, look.z * 0.4f), mote, 8, 0.6f);
  if (!(*P)["fx"].empty() && d) { std::string n = d->name; if (startsWith(n, "Potion of ")) n = n.substr(10); lv.fx->message(p.id, n, "good"); }
  return true;
}

void throwPotion(Level& lv, Player& p, ItemStack& held) {
  vec3 look(-std::sin(p.yaw) * std::cos(p.pitch), std::sin(p.pitch), -std::cos(p.yaw) * std::cos(p.pitch));
  auto f = std::make_unique<PotionFlask>();
  f->realm = p.realm; f->potion = held.id; f->owner = p.id;
  f->pos = p.pos + vec3(0, 1.62f - 0.1f, 0) + look * 0.5f;
  f->vel = look * 11.f + vec3(0, 2.5f, 0);
  f->data["potion"] = held.id;
  lv.spawn(std::move(f));
  if (!p.creative() && --held.n <= 0) held.clear();
  lv.fx->sound("bow", p.pos + vec3(0, 1.4f, 0), 16);
}

void splashPotionAt(Level& lv, int realm, vec3 at, const std::string& key, Player* owner) {
  lv.fx->sound("glass_break", at, 24);
  const json* P = potionInfo(key);
  if (!P) return;
  const json& fx = (*P)["fx"];
  std::string col = !fx.empty() && magicData().effect(fx[0][0]) ? fx[0][0].get<std::string>() : "";
  lv.fx->particles(col.empty() ? "brew_bubble" : "fxmote_" + col, at + vec3(0, 0.1f, 0), 40, json{{"m", "splash"}});
  auto isInstant = [](const std::string& e) { return e == "heal" || e == "harm" || e == "mana"; };
  if (P->value("P", "") == "water") {   // water puts out fire
    for (auto& e : lv.entities()) if (!e->removed && e->realm == realm && glm::distance(e->pos + vec3(0, e->isPlayer() ? 0.9f : 0, 0), at) < 4) e->data.erase("burnT");
    return;
  }
  for (auto& up : lv.entities()) {
    Entity& e = *up;
    if (e.removed || e.realm != realm) continue;
    if (e.isPlayer()) {
      Player& q = static_cast<Player&>(e);
      if (q.dead || q.ghost) continue;
      float dist = glm::distance(q.pos + vec3(0, 0.9f, 0), at);
      if (dist > 4) continue;
      float k = owner == &q && dist < 1.5f ? 1 : 1 - dist / 4;
      for (auto& f : fx) { std::string eff = f[0]; float t = f[1]; int l = f.size() > 2 && f[2].is_number() ? f[2].get<int>() : 0;
        giveEffect(lv, q, eff, isInstant(eff) ? std::max(1.f, std::round(t * k)) : t * 0.75f * k, l, owner); }
    } else if (isMob(e) && isAlive(e)) {
      float dist = glm::distance(e.pos + vec3(0, e.h / 2, 0), at);
      if (dist > 4) continue;
      float k = 1 - dist / 4;
      bool harmful = false;
      for (auto& f : fx) { std::string eff = f[0]; float t = f[1]; int l = f.size() > 2 && f[2].is_number() ? f[2].get<int>() : 0;
        mobEffect(lv, e, eff, isInstant(eff) ? std::max(1.f, std::round(t * k)) : t * 0.75f * k, l, owner);
        harmful |= eff == "harm" || eff == "poison" || eff == "slow" || eff == "weak"; }
      const json& T = mobTemplate(e);
      if (harmful && owner && !(T.is_object() && T.value("hostile", false))) e.data["panicT"] = 4;   // animals just run
    }
  }
}

bool fillBottle(Level& lv, Player& p, ItemStack& held, ivec3 hit, int face) {
  if (held.id != "glass_bottle" && held.id != "potion_water") return false;
  World& w = lv.world(p.realm);
  auto fill = [&]() {
    ItemStack full("potion_water", 1);
    if (--held.n <= 0) held = full; else giveOrDrop(lv, p, full);
    lv.fx->sound("bucket", p.pos + vec3(0, 1, 0), 12);
  };
  if (face >= 0) {
    BlockId id = w.getBlock(hit.x, hit.y, hit.z);
    const BlockDef& b = reg().block(id);
    if (b.has("cauldron")) {
      u8 m = w.getMeta(hit.x, hit.y, hit.z), l = m & 3;
      if (held.id == "glass_bottle" && l > 0) { w.setBlock(hit.x, hit.y, hit.z, id, (u8)((m & ~3) | (l - 1))); fill(); return true; }
      if (held.id == "potion_water" && l < 3) { w.setBlock(hit.x, hit.y, hit.z, id, (u8)((m & ~3) | (l + 1))); held = ItemStack("glass_bottle", 1); lv.fx->sound("splash_small", vec3(hit) + vec3(0.5f), 12); return true; }
    }
    if (b.has("hive") || b.key == "bee_nest" || b.key == "beehive") return false;
  }
  if (held.id != "glass_bottle") return false;
  vec3 look(-std::sin(p.yaw) * std::cos(p.pitch), std::sin(p.pitch), -std::cos(p.yaw) * std::cos(p.pitch));
  RayHit h = raycastBlocks(w, glm::dvec3(p.pos) + glm::dvec3(0, 1.62, 0), look, 5.f, true);
  if (h.hit && reg().tLiquid[h.id] == 1) { fill(); return true; }
  return false;
}

bool breakXpBottle(Level& lv, Player& p, ItemStack& held) {
  const ItemDef* d = held.def();
  if (!d || !d->has("xpBottle")) return false;
  if (!p.creative() && --held.n <= 0) held.clear();
  lv.fx->sound("glass_break", p.pos + vec3(0, 1.3f, 0), 16); lv.fx->soundUi(p.id, "levelup");
  vec3 look(-std::sin(p.yaw), 0, -std::cos(p.yaw));
  vec3 at = p.pos + vec3(look.x * 0.8f, 1.3f, look.z * 0.8f);
  for (int k = 0; k < 16; k++) sfx::spark(lv, at, k % 2 ? "mana_light" : "ench_glyph_" + std::to_string(k % 6), json{{"vx", (rnd() - 0.5) * 2}, {"vy", 1 + rnd() * 1.5}, {"vz", (rnd() - 0.5) * 2}, {"life", 0.8}, {"size", 0.08}});
  vec3 tp = p.pos + vec3(look.x, 1.6f, look.z);
  grantXP(lv, p, 4 + (int)std::floor(rnd() * 8), &tp);
  return true;
}

std::string potionTooltip(const std::string& id) {
  const json* P = potionInfo(id);
  std::string out;
  if (!P) { if (isBrewIngredient(id)) out = "A brewing ingredient"; return out; }
  bool splash = P->value("splash", false);
  for (auto& f : (*P)["fx"]) {
    std::string eff = f[0]; float t = f[1]; int l = f.size() > 2 && f[2].is_number() ? f[2].get<int>() : 0;
    if (!out.empty()) out += "\n";
    auto num = [](float v) { return std::fmod(v, 1.f) == 0 ? std::to_string((int)v) : strFormat("%g", v); };
    if (eff == "heal") out += "Heals " + num(t / 2) + " hearts" + (splash ? " (less at the edge)" : "");
    else if (eff == "harm") out += "Harms " + num(t / 2) + " hearts" + (splash ? " (less at the edge)" : "");
    else if (eff == "mana") out += "Restores " + num(t) + " mana" + (splash ? " (less at the edge)" : "");
    else if (const EffectDef* E = magicData().effect(eff)) out += E->name + fxRoman(l) + " (" + fmtTime(splash ? t * 0.75f : t) + ")";
  }
  return out;
}

} // namespace dq
