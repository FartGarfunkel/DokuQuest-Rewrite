// ui/glyphs.h — little ink pictures drawn with lines and fills (no art files): the marks on the skill trees' nodes, the
// six stats, the schools of magic and the journal's chapters. Each fits a circle of radius r about its centre, in one
// colour, so it reads as ink on the paper or as a light mark on a coloured node.
#pragma once
#include "common/common.h"
#include <imgui.h>
#include <string>

namespace dq::ui {

enum class Glyph {
  None, Heart, Sword, Dagger, Shield, Drop, Star, Ward, Bolt, Wing, Target, Burst, Arrow, Pierce, Fist, Anvil, Clover,
  Hook, Hourglass, Skull, Flame, Snow, Leaf, Eye, Sun, Swirl, Book, Paw, Crown, Note, Wave, Mountain, Orb,
};

void drawGlyph(ImDrawList* dl, Glyph g, ImVec2 c, float r, ImU32 col);

// what a skill-tree attribute ("health", "spell_power.frost"...) is drawn as
Glyph glyphForAttr(const std::string& attr);
// a school of magic (sim/magic_data: "fire", "frost", "life"...)
Glyph glyphForSchool(const std::string& school);
// a passive node by its first bonus, else by the kind of thing its description says it does ("(inflict effect)"),
// else by its subclass tag; keystones without either get the crown
Glyph glyphForNode(const std::string& firstAttr, const std::string& desc, const std::string& tag, int size);

} // namespace dq::ui
