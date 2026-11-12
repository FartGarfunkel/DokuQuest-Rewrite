// ui/hud_combat.h — the combat batch's HUD (ui/hud_combat.cpp): stamina ring, shield and parry, the creature plate
#pragma once
struct ImDrawList;
namespace dq {
class GameSession;
void drawCombatHud(GameSession& s, ImDrawList* dl, float z, float W, float H);
}  // namespace dq
