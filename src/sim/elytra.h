// sim/elytra.h — elytra, the wings from the End ships (game/elytra.js, 0.9.0.19).
// Worn in the cape slot (contentHooks().capeSlot; until the characters area adds it, elytra in the chest armour
// slot count). Jump while falling and they spread: you glide where you look. Dive to gather speed, pull up to
// climb (it costs speed), land low and slow; hit a wall fast and it hurts. A gust puff or a firework rocket used
// in flight throws you forward.
//
// For the player controller (gameplay): on the jump press call elytraJump(); while Player::gliding is set call
// elytraMove() instead of the walking/swimming movement. Until it does, render/content_layers.cpp drives the
// local player's glide right after its movement. Characters: Player::gliding is the pose flag (arms swept back,
// legs together, wings open by ELYTRA_SPAN); elytraWings() gives the two panels.
#pragma once
#include "sim/content.h"

namespace dq {

constexpr float ELYTRA_SPAN = 1.15f;           // how far the wings spread in flight (radians out from the back)
inline bool g_elytraByController = false;      // gameplay sets this once its controller calls elytraJump/elytraMove
bool hasElytra(const Player& p);
bool elytraJump(Level& lv, Player& p);                  // the jump edge: spread the wings if falling (true = started)
void elytraMove(Level& lv, Player& p, double dt);       // a glide step (moves p; ends the glide when it must)
// the wing panels in the cape's frame (x right, y down from the shoulders, z back; pixels of 1/16 block):
// each a W 9 x L 21 x T 1.2 box turned about its top inner corner by `side * (0.16 + spread)`, tipped back by spread * 0.35
struct ElytraWing { float side, ang, tilt; vec3 min, max; };
std::array<ElytraWing, 2> elytraWings(bool open);

} // namespace dq
