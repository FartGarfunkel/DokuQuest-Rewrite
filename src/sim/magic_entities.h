// sim/magic_entities.h — the moving things of the magic modules:
//   SpellBolt  "spell_bolt"   firebolt / frost shard (magic.js SpellBolt), arcane missiles that seek and the
//                             fireball that bursts (arcana.js ArcBolt). netLocal like the original's
//                             netLocalOnly: every machine draws its own from the "cast" spell effect.
//   PotionFlask "potion_flask" a thrown splash potion (brewing.js over monsters.js HexFlask).
#pragma once
#include "sim/magic_combat.h"

namespace dq {

class SpellBolt : public Entity {
public:
  SpellBolt() { type = "spell_bolt"; w = 0.2f; h = 0.3f; noGravity = true; netLocal = true; hp = maxHp = 1; }
  std::string spell;     // firebolt, frost, missiles, fireball
  u32 caster = 0, target = 0;
  float dmg = 0;
  void tick(Level& lv, double dt) override;
  void hit(Level& lv, Entity* e);
  json save() const override;
  void load(const json& j) override;
};

class PotionFlask : public Entity {
public:
  PotionFlask() { type = "potion_flask"; w = 0.15f; h = 0.3f; hp = maxHp = 1; }
  std::string potion;    // the splash potion's item key
  u32 owner = 0;
  void tick(Level& lv, double dt) override;
  void smash(Level& lv);
  json save() const override;
  void load(const json& j) override;
};

void registerMagicEntities();

} // namespace dq
