// sim/achievements.cpp — see achievements.h. Most are read once a second from what the player has carried (the Travel
// Journal's data["found"]), where they are and what they are doing; kills, catches and nights slept come from events.
#include "sim/achievements.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "sim/creature.h"
#include "sim/magic.h"
#include "sim/villagers.h"
#include "core/registry.h"
#include <cstring>
#include <functional>

namespace dq {

const std::vector<AchDef>& achievementList() {
  static const std::vector<AchDef> L = {
    {"wood", "Getting Wood", "Get a log from a tree", "log"},
    {"bench", "Benchmarking", "Make a crafting table", "crafting_table"},
    {"pick", "Time to Mine", "Make a pickaxe", "wooden_pickaxe"},
    {"furnace", "Hot Topic", "Make a furnace", "furnace"},
    {"iron", "Acquire Hardware", "Smelt an iron ingot", "iron_ingot"},
    {"hoe", "Time to Farm", "Make a hoe", "wooden_hoe"},
    {"bread", "Bake Bread", "Make bread from wheat", "bread"},
    {"sword", "Time to Strike", "Make a sword", "wooden_sword"},
    {"monster", "Monster Hunter", "Defeat a monster", "bone"},
    {"leather", "Cow Tipper", "Get some leather", "leather"},
    {"sleep", "Sweet Dreams", "Sleep through a night", "bed"},
    {"fish", "Fishy Business", "Catch something on a line", "fishing_rod"},
    {"village", "Village People", "Find a village", "bell"},
    {"tame", "Best Friends", "Tame an animal", "bone"},
    {"diamond", "Diamonds!", "Find a diamond", "diamond"},
    {"bookshelf", "Librarian", "Make a bookshelf", "bookshelf"},
    {"enchant", "Enchanter", "Make an enchanting table", "enchanting_table"},
    {"forge", "Forgemaster", "Forge a tool head at the forge", "iron_ingot"},
    {"spell", "First Words", "Learn a spell", "book"},
    {"paint", "Fresh Paint", "Paint some planks", "paintbrush"},
    {"nether", "We Need to Go Deeper", "Go down into the Nether", "obsidian"},
    {"blaze", "Into Fire", "Get a blaze rod", "blaze_rod"},
    {"sky", "Up in the Clouds", "Climb to the Skylands", "feather"},
    {"end", "The End?", "Reach the End", "end_stone"},
    {"dragon", "The End.", "Defeat the Ender Dragon", "dragon_egg"},
    {"glide", "Wings", "Glide on elytra", "elytra"},
    {"wither", "Withering Heights", "Defeat the Wither", "nether_star"},
    {"storm", "The Calm", "Break the Wither Storm's heart", "rift_core"},
  };
  return L;
}

namespace {
bool has(const json& d, const char* id) { const json* a = d.is_object() && d.contains("ach") ? &d["ach"] : nullptr; return a && a->is_object() && a->contains(id); }

void grant(Level& lv, Player& p, const char* id) {
  if (!p.data.is_object()) p.data = json::object();
  if (has(p.data, id)) return;
  json& a = p.data["ach"]; if (!a.is_object()) a = json::object();
  a[id] = unixMillis() / 1000;
  DQ_LOG("achievement %s: %s", p.name.c_str(), id);
  for (auto& d : achievementList()) if (std::string(d.id) == id) { lv.fx->message(p.id, std::string("Achievement: ") + d.title, "achievement"); break; }
  lv.fx->soundUi(p.id, "levelup");
}
Player* byName(Level& lv, const json& n) {
  if (!n.is_string()) return nullptr;
  for (auto& e : lv.entities()) if (auto* p = dynamic_cast<Player*>(e.get())) if (!p->removed && p->name == n.get<std::string>()) return p;
  return nullptr;
}
bool foundAny(const json& found, const std::function<bool(const std::string&)>& f) {
  if (!found.is_object()) return false;
  for (auto& [k, v] : found.items()) if (f(k)) return true;
  return false;
}
bool ends(const std::string& k, const char* e) { size_t n = std::strlen(e); return k.size() >= n && k.compare(k.size() - n, n, e) == 0; }
}

void registerAchievements() {
  static bool done = false;
  if (done) return;
  done = true;
  registerLevelSystem([](Level& lv) -> std::shared_ptr<LevelSystem> {
    struct S : LevelSystem {
      Level* L = nullptr; double t = 0; std::vector<int> hs;
      const char* name() const override { return "achievements"; }
      ~S() override { if (L && L->events) for (int h : hs) L->events->off(h); }
      void tick(Level& lv, double dt) override {
        if (lv.isRemote() || (t += dt) < 1) return;
        t = 0;
        for (auto& e : lv.entities()) {
          auto* p = dynamic_cast<Player*>(e.get());
          if (!p || p->removed || p->dead) continue;
          const json found = p->data.is_object() && p->data.contains("found") ? p->data["found"] : json::object();
          auto f = [&](const char* k) { return found.is_object() && found.contains(k); };
          if (foundAny(found, [](const std::string& k) { return k == "log" || ends(k, "_log") || ends(k, "_stem"); })) grant(lv, *p, "wood");
          if (f("crafting_table")) grant(lv, *p, "bench");
          if (foundAny(found, [](const std::string& k) { return ends(k, "_pickaxe"); })) grant(lv, *p, "pick");
          if (f("furnace")) grant(lv, *p, "furnace");
          if (f("iron_ingot")) grant(lv, *p, "iron");
          if (foundAny(found, [](const std::string& k) { return ends(k, "_hoe"); })) grant(lv, *p, "hoe");
          if (f("bread")) grant(lv, *p, "bread");
          if (foundAny(found, [](const std::string& k) { return ends(k, "_sword"); })) grant(lv, *p, "sword");
          if (f("leather")) grant(lv, *p, "leather");
          if (f("diamond")) grant(lv, *p, "diamond");
          if (f("bookshelf")) grant(lv, *p, "bookshelf");
          if (f("enchanting_table")) grant(lv, *p, "enchant");
          if (foundAny(found, [](const std::string& k) { const ItemDef* d = reg().item(k); return d && d->has("forgeHead"); })) grant(lv, *p, "forge");
          if (foundAny(found, [](const std::string& k) { return ends(k, "_painted_planks"); })) grant(lv, *p, "paint");
          if (f("blaze_rod")) grant(lv, *p, "blaze");
          if (!knownSpells(*p).empty()) grant(lv, *p, "spell");
          if (p->gliding) grant(lv, *p, "glide");
          if (p->realm == REALM_MAIN) {
            const int d = dimOfY(ifloor(p->pos.y));
            if (d == DIM_NETHER) grant(lv, *p, "nether");
            if (d == DIM_END) grant(lv, *p, "end");
            if (d == DIM_AETHER) grant(lv, *p, "sky");
            if (d == DIM_OVERWORLD && villageAtPos(lv, p->realm, p->pos, 8)) grant(lv, *p, "village");
          }
          for (auto& o : lv.entities()) if (auto* c = dynamic_cast<Creature*>(o.get())) if (c->tamed && c->owner == p->name) { grant(lv, *p, "tame"); break; }
        }
      }
    };
    auto s = std::make_shared<S>(); s->L = &lv;
    if (lv.events) {
      s->hs.push_back(lv.events->on("mobDeath", [&lv](const json& a) {   // [{type...}, killer name]
        if (lv.isRemote() || !a.is_array() || a.size() < 2) return true;
        Player* p = byName(lv, a[1]);
        if (!p || !a[0].is_object()) return true;
        const std::string type = a[0].value("type", std::string());
        if (type == "ender_dragon") grant(lv, *p, "dragon");
        if (type == "wither") grant(lv, *p, "wither");
        if (const CreatureType* ct = creatureType(type); ct && ct->hostile) grant(lv, *p, "monster");
        return true;
      }));
      s->hs.push_back(lv.events->on("stormBroken", [&lv](const json&) {
        if (lv.isRemote()) return true;
        for (auto& e : lv.entities()) if (e && !e->removed && e->isPlayer()) grant(lv, *static_cast<Player*>(e.get()), "storm");
        return true;
      }));
      s->hs.push_back(lv.events->on("fishCaught", [&lv](const json& a) { if (!lv.isRemote() && a.is_array() && !a.empty()) if (Player* p = byName(lv, a[0])) grant(lv, *p, "fish"); return true; }));
      s->hs.push_back(lv.events->on("morning", [&lv](const json&) {
        if (lv.isRemote()) return true;
        for (auto& e : lv.entities()) if (auto* p = dynamic_cast<Player*>(e.get())) if (!p->removed && !p->dead) grant(lv, *p, "sleep");
        return true;
      }));
    }
    return s;
  });
}

} // namespace dq
