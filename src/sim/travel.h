// sim/travel.h — roads and reins (game/travel.js, 0.9.0.36).
// Waystones: every village gets one in its square the first time someone comes by (needs
// contentHooks().villageNear); use one to attune to it, and to travel to any other you're attuned to for a
// level or two of experience by how far (a level per 600 blocks, at most 5). Your own (crafted) ones are named
// for you. Records: LevelInfo.data["waystones"] (the villages'), Player::stats["ways"] (yours), block entity
// {type:"waystone", wayId, name}. The menu is ui/content_screens.cpp (makeWaystoneScreen).
// The measure of a mount: speed, jump and toughness (Entity::data["hs"] = [s, j, t], 0.7..1.5), for the creatures
// area's riding and breeding. The carriage (a boat type) is the vehicles area's.
#pragma once
#include "sim/content.h"

namespace dq {

extern const char* const DIR_WORDS[8];
std::string dirWord(float dx, float dz);                 // "north-east"... (north = -z)
int wayCost(float dist);
std::string wayIdAt(World& w, int x, int y, int z);
std::string wayNameAt(World& w, int x, int y, int z);
std::string wayAttune(Level& lv, Player& p, int x, int y, int z);   // returns the waystone's id
std::string wayGo(Level& lv, Player& p, const std::string& id);     // "" = gone there, else why not (also toasted)
struct WayEntry { std::string id, name; ivec3 pos; float dist = 0; int cost = 1; std::string dir; bool can = false; };
std::vector<WayEntry> wayList(Level& lv, Player& p, const std::string& hereId);   // the others in this layer, nearest first
std::optional<ivec3> waySiteFor(Level& lv, const VillageInfo& v);

// mounts (MOUNTS: base speed, jump)
std::optional<vec2> mountBase(const std::string& type);
json mountStats(Entity& m);                              // creates the measure the first time (and sets toughness' hp)
std::string hsLine(const json& hs);                      // "Speed ★★★☆☆ · Jump ★★★★☆ · Toughness ★★☆☆☆"
json foalStats(const json& a, const json& b);            // a foal takes after both parents, now and then doing better
void hsApplyHp(Entity& m);

} // namespace dq
