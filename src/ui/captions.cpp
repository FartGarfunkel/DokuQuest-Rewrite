// ui/captions.cpp — captions for the sounds around you (BTA's subtitles; Options → Accessibility → Captions): what a
// played sound is, in a few words. client/client_fx.cpp asks as each world sound plays; ui/hud.cpp shows the last few at
// the side of the screen with the way they come from. Sounds that are only the interface (clicks, your own steps) say
// nothing.
#include "ui/ui.h"
#include "core/registry.h"
#include <map>
#include <set>

namespace dq {

static std::string humanize(std::string s) {
  for (auto& c : s) if (c == '_' || c == '.' || c == '/') c = ' ';
  while (!s.empty() && (s.back() == ' ' || isdigit((unsigned char)s.back()))) s.pop_back();
  if (!s.empty()) s[0] = (char)toupper((unsigned char)s[0]);
  return s;
}

std::string captionForSound(const std::string& name) {
  static const std::set<std::string> QUIET = {"click", "pop", "equip", "ui", "hover", "page", "tick", "select", "back", "type"};
  static const std::map<std::string, std::string> T = {
    {"door_open", "Door opens"}, {"door_close", "Door shuts"}, {"chest", "Chest opens"}, {"chest_close", "Chest shuts"},
    {"explode", "Explosion"}, {"explosion", "Explosion"}, {"hit", "Something is hit"}, {"hurt", "Someone is hurt"},
    {"levelup", "Level up"}, {"xp", "Experience"}, {"splash", "Splash"}, {"swim", "Swimming"}, {"portal", "Portal hums"},
    {"portal_travel", "Portal whooshes"}, {"ignite", "Fire is lit"}, {"fire", "Fire crackles"}, {"fizz", "Hiss"},
    {"anvil", "Anvil rings"}, {"thunder", "Thunder"}, {"rain", "Rain"}, {"bell", "Bell rings"}, {"bow", "Bow twangs"},
    {"arrow", "Arrow flies"}, {"arrow_hit", "Arrow hits"}, {"eat", "Eating"}, {"drink", "Drinking"}, {"burp", "Burp"},
    {"break", "Something breaks"}, {"glass", "Glass breaks"}, {"pickup", "Item picked up"}, {"lever", "Lever clicks"},
    {"button", "Button clicks"}, {"piston", "Piston moves"}, {"note", "Note block plays"}, {"jukebox", "Music plays"},
    {"rift_open", "Something tears open"}, {"rift_whisper", "Whispers"}, {"nightmare", "You wake with a start"},
    {"eruption", "Volcano erupts"}, {"nether_rumble", "Ground rumbles"}, {"acid_bubble", "Acid bubbles"},
    {"crystal_chime", "Crystals ring"}, {"ash_moan", "Wind moans"}, {"crag_crack", "Stones fall"},
    {"shear", "Shearing"}, {"milk", "Milking"}, {"bucket", "Bucket fills"}, {"bucket_empty", "Bucket empties"},
    {"furnace", "Furnace crackles"}, {"brew", "Brewing bubbles"}, {"enchant", "Enchanting"}, {"spell", "A spell is cast"},
    {"teleport", "Teleport"}, {"thud", "Thud"}, {"land", "Landing"}, {"fall", "Something falls"}, {"cast", "Fishing line cast"},
    {"reel", "Fishing line reels in"}, {"bobber", "Bobber splashes"}, {"wind", "Wind"}, {"boom", "Boom"}, {"chime", "Chime"},
  };
  if (name.empty() || QUIET.count(name)) return "";
  if (auto it = T.find(name); it != T.end()) return it->second;
  // "zombie.say", "dig_stone", "step.grass"...: the first word, then a word for what it does
  std::string base = name, rest;
  if (auto d = name.find_first_of("._"); d != std::string::npos) { base = name.substr(0, d); rest = name.substr(d + 1); }
  if (base == "step" || base == "ui" || rest == "step") return "Footsteps";
  if (base == "dig") return "Digging";
  return humanize(name);
}

std::string captionForVoice(const std::string& creature, const std::string& kind) {
  std::string label = creature;
  const json& M = reg().mobs.contains(creature) ? reg().mobs[creature] : json();
  if (M.is_object() && M.contains("label") && M["label"].is_string()) label = M["label"].get<std::string>();
  if (label.rfind("a ", 0) == 0) label = label.substr(2); else if (label.rfind("an ", 0) == 0) label = label.substr(3); else if (label.rfind("the ", 0) == 0) label = label.substr(4);
  label = humanize(label);
  if (kind == "hurt") return label + " hurts";
  if (kind == "death" || kind == "die") return label + " dies";
  if (kind == "attack" || kind == "angry") return label + " attacks";
  if (kind == "step") return "Footsteps";
  return label;   // "say" and the rest: it makes its noise
}

std::string captionForMaterial(const std::string& mat, const std::string& kind) {
  if (kind == "step") return "Footsteps";
  const std::string m = humanize(mat);
  if (kind == "break") return m + " breaks";
  if (kind == "place") return m + " placed";
  if (kind == "dig" || kind == "hit") return "Digging";
  return "";
}

} // namespace dq
