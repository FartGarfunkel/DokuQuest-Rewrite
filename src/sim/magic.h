// sim/magic.h — magic (game/magic.js, arcana.js, spellcraft.js, the schools in paths.js): spells learned from
// tomes and cast with a staff (or bare-handed for the newer spells, weaker), mana and its regeneration,
// cooldowns, the schools that level as you cast, robes, the Alteration spells at work (witchlight,
// levitation, stoneskin, ward), summons, and the item uses of the magic/brewing/enchanting modules.
// Runs on whichever machine simulates the player (the host, or a guest for its own player); creatures and
// block entities only where the world is simulated (!lv.isRemote()).
#pragma once
#include "sim/magic_data.h"
#include "sim/magic_combat.h"

namespace dq {

// What a player has going on that isn't saved (the original kept these on the Player object).
struct MagicState {
  float mana = -1;                 // < 0 = not set yet (fills to manaMax)
  float castCd = 0;
  float ward = 0, wardT = 0;       // Ward spell / Warding potion
  float lightT = 0, levT = 0, stoneT = 0, slowFallT = 0;
  float poison = 0, poisonT = 0;   // venom timer (monsters.js), its tick
  float regT = 0, secondWindT = 0, exT = 0, stT = 0, gtT = 0, saveT = 0;
  bool wardGot = false;
  vec3 witch{0}; bool witchSet = false;   // the witchlight's spot
  bool bare = false;               // casting without a staff (Z)
  bool jumpHeld = false;           // set by the controller each frame (levitation: jump to rise)
  std::string lastSpell;           // shown by the HUD
};

class MagicSystem;
MagicState& magicState(Level& lv, const Player& p);

// ---- mana ----
float manaMax(const Player& p);
float manaRegen(const Player& p);
float robeBoost(const Player& p, const std::string& spell);   // a whole robe set: its school a third stronger (archmage 1.12)
const json* heldStaff(const Player& p);                       // the held item's `staff` def, or null

// ---- spells ----
std::string activeSpell(Player& p);                           // "" = none known
void cycleSpell(Level& lv, Player& p);                        // G / sneak + use
bool pickSpell(Level& lv, Player& p, const std::string& k);   // the spellbook
bool learnTome(Level& lv, Player& p, ItemStack& tome);
bool castSpell(Level& lv, Player& p);                         // with the held staff; false = not a staff in hand
void castQuick(Level& lv, Player& p);                         // Z: with or without a staff
bool spellRepeats(const std::string& k);                      // held button repeats the cast
const std::vector<std::string>& knownSpells(const Player& p); // (reads stats.spells)
int schoolLv(const Player& p, const std::string& school);
std::string schoolOfSpell(const std::string& k);

// ---- incoming harm (magic.js ward + stoneskin, brewing.js potions, enchanting.js protection, skills.js
// Defence, rpg.js perks, paths.js Mage's mana shield). The gameplay hurt calls this once before armour:
// returns what is left of `amount` (src may be updated: knock, _raw). Then call afterPlayerHurt with what
// was actually taken (thorns, Defence experience, second wind).
float magicIncomingDamage(Level& lv, Player& p, float amount, json& src);
void afterPlayerHurt(Level& lv, Player& p, float took, const json& src);
// outgoing harm by a player's spell on a creature (paths.js Elemental Focus / Overload)
float magicOutgoingDamage(Level& lv, Player& p, float amount, const json& src);

// ---- item uses (itemUse handler; returns false = used up) ----
bool magicItemUse(Level& lv, Player& p, const std::string& key, ivec3 at, int face);

// alchValue (spellcraft.js): what an item is worth to Low Alchemy
double alchValue(const std::string& id);

void registerMagic();      // called once from initCore()

void spawnFamiliar(Level& lv, Player& p, float power);   // stable.js (the host does it for a guest)

// spells cast (sim/magic_spells.cpp)
struct CastCtx;
bool castOldSpell(Level& lv, Player& p, const std::string& k, const json& staff, float power, CastCtx& c);
bool castNewSpell(Level& lv, Player& p, const std::string& k, const json& staff, float power, CastCtx& c);

} // namespace dq
