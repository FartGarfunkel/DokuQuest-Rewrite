// render/instruments.h — a compass and a clock you read by looking at them (game/instruments.js).
// The compass needle swings round to point at your spawn (and spins in other dimensions); the death compass
// points to your grave (Player::stats["grave"] {dim, x, z}); the clock's dial turns to show the sun or the moon.
// The pictures are painted frame by frame like the original (32 / 64 / 32 frames, 16 px, outlined) and shown
// live: over the item in your hand (HeldItemLayer::itemHook), and for the UI through instrumentIcon().
#pragma once
#include "render/content_draw.h"

namespace dq {

class GameSession;
constexpr int COMPASS_FRAMES = 32, CLOCK_FRAMES = 64;
struct InstrumentState { float compassA = 0, compassV = 0, deathA = 0, deathV = 0; int compassF = 0, clockF = 0, deathF = 0; };
InstrumentState& instruments();
void updateInstruments(GameSession& s, double dt);       // each frame (render/content_layers.cpp)
bool isInstrument(const std::string& key);               // compass, clock, death_compass
// the frame to show now: a GL texture (a strip of 16 px frames) and the frame's uv rect (u0, v0, u1, v1).
// UI: ImGui::Image((ImTextureID)(intptr_t)tex, size, {uv.x, uv.y}, {uv.z, uv.w}) in place of the static icon.
bool instrumentIcon(const std::string& key, GLuint& tex, vec4& uv);
// pure: one frame's 16 x 16 RGBA (the painting), for tests and atlases
std::vector<u8> paintInstrumentFrame(const std::string& key, int frame);
void registerInstruments();                              // installs the held-item hook (once)

} // namespace dq
