// sim/rpg.h — the long road (game/rpg.js): perks at 30/60/90 in every skill, Exploring experience (places
// found, new lands, other worlds, ground covered), Alchemy experience (brews, enchantments), the Realm
// Fragments from great foes, the story as journal chapters with your records; and the Mage path's talents
// from game/paths.js (the path itself is chosen from villagers — see docs/port/magic.md).
#pragma once
#include "sim/skills.h"

namespace dq {

bool hasPerk(const Player& p, const std::string& skill, int lvl);

// paths.js: stats.path = { k: "knight"|"ranger"|"mage", t: { talent: 1 } }
bool onPath(const Player& p, const std::string& k);
bool hasTal(const Player& p, const std::string& t);
std::string pathSkill(const Player& p);

// a chapter of the journal
struct StoryChapter { std::string t, goal, hint; bool done = false, side = false, epi = false; float prog = -1; };
struct Story { std::vector<StoryChapter> ch; int cur = -1; };
Story storyOf(Level& lv, Player& p);
std::vector<std::pair<std::string, std::string>> journalRecords(Level& lv, Player& p);
std::vector<std::string> greatFoesBeaten(Level& lv);

// the terrain code names the biome at x,z (Exploring: "New land: ..."); none set = no biome experience
void setBiomeNamer(std::function<std::string(Level&, int x, int z)> f);

void rpgTick(Level& lv, Player& p, double dt);      // exploring, green thumb, the iron pickaxe chapter, second wind timer
void onBrewDone(Level& lv, int realm, vec3 at, int made, ItemStack* sample);   // Alchemy for the nearest player; Distiller
void onEnchanted(Level& lv, Player& p, int levelsSpent);                        // Alchemy; Philosopher gives a level back
void onGreatFoeDied(Level& lv, Entity& mob);                                    // Realm Fragments (mobDeath)

} // namespace dq
