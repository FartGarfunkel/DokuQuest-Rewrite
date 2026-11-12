// ui/magic_screens.h — the magic workstream's screens (plain ImGui, restyled with ui/theme.* at merge):
//   spellbook (B, or sneak + d-pad up)   game/spellcraft.js openSpellbook
//   skills and perks (K)                 game/skills.js openSkills + rpg.js perks
//   journal (J)                          game/rpg.js openJournal
//   enchanting table                     game/enchanting.js (the station panel), opened by using the block
//   brewing stand                        game/brewing.js (the brew panel), opened by using the block
// Every one works with mouse, keyboard and controller (ImGui navigation; B/Esc closes).
#pragma once
#include "ui/screen.h"

namespace dq {

std::unique_ptr<Screen> makeSpellbookScreen();
std::unique_ptr<Screen> makeSkillsScreen();
std::unique_ptr<Screen> makeJournalScreen();
std::unique_ptr<Screen> makeEnchantScreen(ivec3 tablePos);
std::unique_ptr<Screen> makeBrewScreen(ivec3 standPos);

// the client side of magic: the spell-effects layer (particles, bolts, witchlight, the floating book), the HUD
// (mana bar, effect chips, skill pops), the keys (G, B, Z, K, J) and opening the two stations. Call once from
// GameSession's constructor (idempotent).
void registerMagicClient();

// shared by the screens and the HUD: the stack's tooltip lines (enchantments, potion effects, gear level)
std::string magicItemTooltip(const struct ItemStack& s);

} // namespace dq
