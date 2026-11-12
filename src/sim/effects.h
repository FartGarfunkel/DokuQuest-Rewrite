// sim/effects.h — potion and spell effects (game/brewing.js EFFECTS, arcana.js, spellcraft.js giveEffect):
// speed, slow, jump, strength, weak, regen, poison, fireres, water, night, invis, slowfall, resist, clarity,
// fortune, haste, warding — each with seconds left and a level (0 = I). Instant ones: heal, harm, mana.
// A player's effects are kept in Player::stats["fx"] = { effect: [secondsLeft, level] } (saved, as the
// original's Player.toJSON `fx`). Creatures only feel instant ones and simple statuses (mobEffect).
// NOTE for the merge: gameplay may bring its own status effects (poison/frost/fire on hit); this is the
// original brewing.js list — reconcile into one system (the ids and numbers here are the original's).
#pragma once
#include "sim/magic_combat.h"

namespace dq {

struct ActiveEffect { std::string id; float secs = 0; int lv = 0; };

// Give `p` an effect (instant: "heal"/"harm"/"mana" with an amount). Alchemy's Steeped perk lengthens good
// ones; a stronger running effect is kept (brewing.js rules).
void giveEffect(Level& lv, Player& p, const std::string& eff, float secs, int level = 0, Entity* src = nullptr);
void clearEffects(Player& p);                                  // milk, death
float effectTime(const Player& p, const std::string& eff);     // seconds left (0 = not under it)
int effectLevel(const Player& p, const std::string& eff);      // 0 = I, 1 = II ...
inline bool hasEffect(const Player& p, const std::string& eff) { return effectTime(p, eff) > 0; }
std::vector<ActiveEffect> effectsOn(const Player& p);
void tickEffects(Level& lv, Player& p, double dt);            // countdown, regeneration, poison, slow fall, fire resistance, warding

// a splash potion or a tipped blade on a creature
void mobEffect(Level& lv, Entity& e, const std::string& eff, float secs, int level, Player* src);

// ---- what they do (for the player controller / combat) ----
float effectSpeedFactor(const Player& p);        // Speed / Slowness (Player.move opts.slow)
float effectJumpFactor(const Player& p);         // Jump Boost: multiply the jump velocity
float effectDamageFactor(const Player& p);       // Strength / Weakness: a share of the blow
float effectBreakDivisor(const Player& p);       // Haste: break time / this
float effectNightVision(const Player& p);        // 0..1 (blinks out over the last ten seconds)
bool effectBreathes(const Player& p);            // Water Breathing: keep air full
bool hiddenFrom(const Player& p, float dist, bool mobWasHurt);   // Invisibility: creatures notice only within 3.5
std::string fmtTime(float s);                    // 1:05
std::string fxRoman(int lv);                     // "", " II", ...

} // namespace dq
