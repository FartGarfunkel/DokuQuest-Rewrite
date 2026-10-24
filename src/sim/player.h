// sim/player.h — a player (game/player.js + local.js rules live in sim/ systems).
// No hunger (the original has none): health regenerates 1 HP / 4 s, food heals.
#pragma once
#include "sim/entity.h"
#include "core/itemstack.h"

namespace dq {

constexpr int INV_MAIN = 36;     // 0..8 hotbar, 9..35 backpack rows
constexpr int INV_ARMOR = 4;     // head, chest, legs, feet
constexpr int INV_OFFHAND = 1;

class Player : public Entity {
public:
  Player() { type = "player"; w = 0.3f; h = 1.8f; hp = maxHp = 20; inv.resize(INV_MAIN); armor.resize(INV_ARMOR); offhand.resize(INV_OFFHAND); }
  std::string name = "Wanderer";
  std::string skin;                 // skin id (file or creator model)
  std::string mode = "survival";    // survival | creative | adventure | spectator
  std::vector<ItemStack> inv, armor, offhand;
  ItemStack cursor;                 // stack held by the mouse in inventory screens
  int selected = 0;                 // hotbar slot
  bool flying = false, sneaking = false, sprinting = false, swimming = false, crawling = false;
  float air = 10, maxAir = 10;
  int xp = 0, level = 0;
  float regenTimer = 0;
  float swingT = 0, eatT = 0, bowPull = 0;   // arm timers the body shows (swing 0..1 progress, 0 = none); render/char_layer
  vec3 spawnPoint{0, -1, 0};        // bed/respawn anchor (y < 0 = world spawn)
  int spawnRealm = 0;
  bool dead = false;
  json stats = json::object();      // rpg/skills/bestiary progress etc. (systems own their keys)

  ItemStack& held() { return inv[selected]; }
  bool creative() const { return mode == "creative"; }
  int give(ItemStack s);            // returns leftover count
  json save() const override;
  void load(const json& j) override;
};

} // namespace dq
