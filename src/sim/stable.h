// sim/stable.h — your stable, World of Warcraft style (game/stable.js, 0.9).
// Every creature you tame, hatch or raise joins your stable. H (or a whistle) shows them all: summon any of them
// to your side wherever you are, dismiss them home again. A companion that falls goes back to the stable to rest
// for 5 minutes. ★ marks your favourite mount: sneak + H calls it, or sends it home.
// Each companion has an id and a generation: summoning or dismissing moves the generation on, so any older copy
// (an unloaded corner, another realm) quietly goes when it next ticks. Only one of each is ever about.
//
// The book: LevelInfo.data["stable"][owner] = [{uid, gen, state: out|stabled|resting, type, name, saddled, variant,
// color, size, baby, hp, maxHp, dim, x, y, z, restUntil, fav, joined, hs}].
// Creatures (Entity::data, shared with the creatures area): "owner" (player name; set when tamed), "uid", "sgen",
// "petName", "saddled", "variant", "color", "size", "baby", "growT", "tamed", "kept", "hs"; "riddenBy" (player id).
// When a companion dies the creatures area calls stableCreatureDies() first: true = it went home to rest (no drops).
#pragma once
#include "sim/content.h"

namespace dq {

constexpr double STABLE_REST = 300;
struct StableEntry {
  std::string uid, type, name, kind, state;   // name: display name; kind: "horse"...; state: out | stabled | resting
  bool named = false, saddled = false, baby = false, here = false, fav = false, mount = false;
  float hp = 0, maxHp = 0, x = 0, z = 0;
  int dim = 0, rest = 0;                       // rest: seconds left
  std::string variant;
};
std::vector<StableEntry> stableList(Level& lv, const std::string& owner);
std::string stableSummon(Level& lv, Player& p, const std::string& uid);    // "" = done, else why not
std::string stableDismiss(Level& lv, Player& p, const std::string& uid);
void stableRename(Level& lv, Player& p, const std::string& uid, const std::string& name);
void stableRelease(Level& lv, Player& p, const std::string& uid);
void stableFav(Level& lv, Player& p, const std::string& uid);
std::string stableQuick(Level& lv, Player& p);                             // sneak + H
bool stableCreatureDies(Level& lv, Entity& m);
bool stableIsMount(const std::string& type);
// the stable screen on a client: the host's answer arrives as a message of kind "stable-list" (JSON array)
json stableListJson(const std::vector<StableEntry>& l);
std::vector<StableEntry> stableListFromJson(const json& j);

} // namespace dq
