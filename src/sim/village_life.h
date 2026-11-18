// sim/village_life.h — what a village gives back to those who are part of it (our own, over villagers.js).
//
// The trades' requests: each villager with a trade (data/villagers.json "tradeWants") asks, each day, for something of
// it (the fisher fish, the farmer crops, the smiths iron and their gear, the baker and the innkeeper dishes, the healer
// potions, the librarian books and lore...): an errand of the quest framework (kind "fetch", "trade": true, given by
// that villager, on the board too) paying coins, standing and experience in the trade's own skill, the asks growing
// with your level in it.
//
// Standing opens doors (Standing::k: 0 Distrusted, 1 Stranger, 2 Visitor, 3 Friend, 4 Trusted, 5 Honoured, 6 Hero):
//   Friend    the trainers (guard, fletcher, librarian, elder) give an advanced lesson: a talent point more on your
//             path (a second where you're Honoured), and forgetting your path costs less there
//   Honoured  the elder offers a house: one of the village's that nobody lives in becomes yours (else the builders
//             raise one): its bed your spawn, its chest yours (the village counts what's in it as yours); the guard
//             has work for the watch: a patrol round the village's posts, someone to see safely to the next village
//   Hero      a statue of you raised in the square (stone, your name on its sign); the village's waystone linked to
//             you, and travel to it free
// Standing lost takes them back: below Hero the waystone costs again, below Honoured the statue comes down and the
// watch has nothing for you, Distrusted (a known thief) and the house is the village's again.
//
// The Shrine of Renewal (block "respec_shrine", in every village square from terrain 25; the elder too): the talent
// tree's nodes and the stat points you spent taken back for coins, more for more spent, less where you stand well.
//
// Records: Player::stats "pathLessons" {vid: n}, "homes" (sim/places.cpp); LevelInfo::data "statues"
// {vid:player: {at, f, blocks}}, "homeClaims" {vid:x0,z0: player}; the waystone's record in stats "ways" carries
// "hero": vid; an escorted villager's Entity::data "escortQ" (their job's id; npc index 300, as the missing).
#pragma once
#include "sim/villagers.h"

namespace dq {

// ---- the trades' requests ----
std::string tradeSkill(const std::string& job);                                // "" = no trade of its own
json tradeRequestFor(Level& lv, const Village& v, int folkIndex, Player& p);    // today's, sized for p (null = none)
json tradeRequests(Level& lv, const Village& v, Player& p);                    // the ones on the board (up to 4)

// ---- a Friend's lessons ----
int pathLessons(const Player& p);                                              // talent points from lessons (all villages)
bool pathLessonOffered(Level& lv, Player& p, const Village& v, const std::string& job, int* price = nullptr);
bool pathLessonTake(Level& lv, Player& p, const Village& v, const std::string& job, std::string& say);
int pathForgetPrice(Level& lv, Player& p);                                     // 20, less for a Friend at a village's trainer

// ---- an Honoured guest's house and the watch ----
bool homeOffered(Player& p, const std::string& vid);                           // Honoured or more
bool homeClaim(Level& lv, Player& p, const Village& v, HeroHouse& out);        // a house nobody lives in made yours
void homeOwn(Level& lv, Player& p, const Village& v, ivec3 bed);               // its bed and chests counted yours
json watchJobs(Level& lv, Player& p, const Village& v);                        // the guard's: a patrol, an escort

// ---- a Hero's statue and waystone ----
bool wayFreeFor(Level& lv, Player& p, const std::string& wayId);               // free travel there (a village's, for its Hero)

// ---- the Shrine of Renewal ----
int respecPoints(const Player& p);                                             // talents + stat points spent
int respecCost(Player& p, const std::string& vid);                             // in gold coins ("" = no village)
bool respecDo(Level& lv, Player& p, const std::string& vid, std::string& say);

// each player, now and then (sim/villager_life.cpp playerLifeTick): statues, links, the watch's jobs, perks lost
void villageLifeTick(Level& lv, Player& p, double dt);

} // namespace dq
