// sim/mount_gear.cpp — a tamed mount's saddle, horse armour and what it carries (sim/mount_gear.h)
#include "sim/mount_gear.h"
#include "sim/creature.h"
#include "sim/player.h"
#include "sim/events.h"
#include "core/registry.h"

namespace dq {

const char* const MOUNT_GEAR_KEYS[6] = {"horseArmor", "horseArmorD", "bags", "pack", "wolfArmor", "wolfArmorD"};

bool mountTakesArmour(const Creature& c) { return !c.baby && c.type == "horse"; }
bool mountTakesChest(const Creature& c) { return !c.baby && (c.type == "donkey" || c.type == "mule" || c.type == "llama" || c.type == "camel"); }

std::string mountPack(const Creature& c) {
  if (!c.data.is_object()) return "";
  if (c.data.contains("pack") && c.data["pack"].is_string()) return c.data["pack"].get<std::string>();   // (a joined world's copy has only this)
  return c.data.contains("bags") && c.data["bags"].is_array() ? "bags" : "";   // (bags put on before there was a "pack")
}
int mountPackSlots(const std::string& pack) { return pack == "chest" ? 27 : pack == "bags" ? 18 : 0; }

static std::string armourKey(const Creature& c) {
  return c.data.is_object() && c.data.contains("horseArmor") && c.data["horseArmor"].is_string() ? c.data["horseArmor"].get<std::string>() : "";
}

std::vector<ItemStack> mountGear(const Creature& c) {
  std::vector<ItemStack> g;
  g.push_back(c.saddled ? ItemStack("saddle", 1) : ItemStack());
  if (mountTakesArmour(c)) {
    const std::string k = armourKey(c);
    g.push_back(k.empty() ? ItemStack() : ItemStack(k, 1, c.data.value("horseArmorD", 0)));
  }
  return g;
}

bool mountGearFits(const Creature& c, int slot, const ItemStack& s) {
  const ItemDef* d = s.def();
  if (!d || c.baby) return false;
  if (slot == 0) return s.id == "saddle";
  if (slot == 1) return mountTakesArmour(c) && d->props.contains("horseArmor");
  return false;
}

void setMountGear(Level& lv, Creature& c, const std::vector<ItemStack>& g) {
  (void)lv;
  const bool sad = !g.empty() && g[0].id == "saddle" && g[0].n > 0;
  if (sad != c.saddled) { c.saddled = sad; c.kept = true; }
  if (g.size() > 1 && mountTakesArmour(c)) {
    const ItemStack& a = g[1];
    if (a.empty() || !mountGearFits(c, 1, a)) { c.data.erase("horseArmor"); c.data.erase("horseArmorD"); }
    else { c.data["horseArmor"] = a.id; c.data["horseArmorD"] = a.d; c.kept = true; }
  }
}

void openMountScreen(Level& lv, Player& p, Creature& c, const std::string& title) {
  if (lv.events) lv.events->emit("screenOpen", json::array({p.name, "container", {{"entity", c.id}, {"name", title}, {"mount", true}}}));
  lv.fx->soundUi(p.id, mountPack(c) == "chest" ? "chest" : "equip");
}

float horseArmourPoints(const Creature& c) {
  const std::string k = armourKey(c);
  const ItemDef* d = k.empty() ? nullptr : reg().item(k);
  return d && d->props.contains("horseArmor") ? d->props["horseArmor"].value("points", 0.f) : 0.f;
}
void horseArmourWear(Creature& c, Level& lv) {
  const std::string k = armourKey(c);
  const ItemDef* d = k.empty() ? nullptr : reg().item(k);
  if (!d || !d->props.contains("horseArmor")) return;
  const int dur = d->props["horseArmor"].value("durability", 0);
  const int used = c.data.value("horseArmorD", 0) + 1;
  if (dur > 0 && used >= dur) { c.data.erase("horseArmor"); c.data.erase("horseArmorD"); lv.fx->sound("break_tool", c.pos, 16); }
  else c.data["horseArmorD"] = used;
}

void mountGearDrops(Level& lv, Creature& c) {
  const vec3 at = c.pos + vec3(0, 0.5f, 0);
  if (c.saddled) { dropItem(lv, c.realm, ItemStack("saddle", 1), at, true); c.saddled = false; }
  if (const std::string k = armourKey(c); !k.empty()) {
    dropItem(lv, c.realm, ItemStack(k, 1, c.data.value("horseArmorD", 0)), at, true);
    c.data.erase("horseArmor"); c.data.erase("horseArmorD");
  }
  if (c.data.contains("bags") && c.data["bags"].is_array()) {   // everything it carried, and the chest or bags
    for (auto& s : c.data["bags"]) { ItemStack st = ItemStack::fromJson(s); if (!st.empty()) dropItem(lv, c.realm, st, at, true); }
    dropItem(lv, c.realm, ItemStack(mountPack(c) == "chest" ? "chest" : "saddle_bags", 1), at, true);
  }
  c.data.erase("bags"); c.data.erase("pack");
}

} // namespace dq
