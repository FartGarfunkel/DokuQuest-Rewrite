// sim/magic_fx.h — the spell visuals of game/magic.js (spellFx, spellSpark, spellRing, spellSpiral,
// castCircle, the beams of spellcraft.js), raised through LevelFx::particles so they reach every player
// near them. kind = the original's spark tile ("mana_fire", "mana_frost", "mana_heal", "mana_arcane",
// "mana_light", "mana_stone", "ench_glyph_0..5", "fxmote_<effect>", "brew_bubble"), extra.m = the shape:
//   fx     n sparks scattered (sp = speed; life, size, spread, vx/vy/vz, grav, grow, flutter)
//   spark  one spark placed exactly (vx/vy/vz, life, size, grav, flat, flutter, grow)
//   ring   n sparks in a ring of radius r spreading out (speed, vy, life, size, flat)
//   spiral n motes rising round a body (r, h, turns, life, size, vy)
//   beam   a line of sparks to `to`
//   circle the circle under a caster's feet (fx = the spell's spark)
//   splash n drops bursting out of a broken bottle
//   cast   a spell was cast here (s = spell, b = [x,y,z,vx,vy,vz] of its bolt, t = homing target id):
//          every client draws the bolt itself (render/magic_fx.cpp)
// The client's spell-effects layer draws these; other particle kinds ("flame", "burst") go to the game's
// particle system.
#pragma once
#include "sim/level.h"

namespace dq::sfx {

inline json opt(json o, const char* m) { if (!o.is_object()) o = json::object(); o["m"] = m; return o; }
inline void burst(Level& lv, vec3 p, const std::string& tile, int n, float speed, json o = json::object()) { o = opt(o, "fx"); o["sp"] = speed; lv.fx->particles(tile, p, n, o); }
inline void spark(Level& lv, vec3 p, const std::string& tile, json o = json::object()) { lv.fx->particles(tile, p, 1, opt(o, "spark")); }
inline void ring(Level& lv, vec3 p, const std::string& tile, float r, int n, json o = json::object()) { o = opt(o, "ring"); o["r"] = r; lv.fx->particles(tile, p, n, o); }
inline void spiral(Level& lv, vec3 p, const std::string& tile, int n, json o = json::object()) { lv.fx->particles(tile, p, n, opt(o, "spiral")); }
inline void beam(Level& lv, vec3 a, vec3 b, const std::string& tile) { lv.fx->particles(tile, a, 1, json{{"m", "beam"}, {"to", {b.x, b.y, b.z}}}); }
inline void circle(Level& lv, vec3 p, const std::string& fxTile) { lv.fx->particles(fxTile, p, 1, json{{"m", "circle"}}); }
inline void flame(Level& lv, vec3 p, int n = 1) { lv.fx->particles("flame", p, n); }
inline void blockBurst(Level& lv, vec3 p, const std::string& block, int n, float speed) { lv.fx->particles("burst", p, n, json{{"tile", block}, {"speed", speed}}); }

} // namespace dq::sfx
