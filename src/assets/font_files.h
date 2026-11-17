// assets/font_files.h — the fonts a player can choose for the game's text and headings (Options → User Interface; the
// launcher uses the same choice): every .ttf / .otf in the game's assets/fonts and in the player's own <user>/fonts.
#pragma once
#include "common/common.h"
#include "common/fs.h"

namespace dq {

constexpr const char* DEFAULT_UI_FONT = "PixelifySans.ttf";
constexpr const char* DEFAULT_TITLE_FONT = "Jersey15.ttf";

std::vector<std::string> fontFiles();                 // file names, the game's own first, sorted
fs::path fontPath(const std::string& name, const char* fallback);   // where that one is (the fallback if it's gone)
std::string fontLabel(const std::string& name);       // "PixelifySans.ttf" -> "Pixelify Sans"
// how tall the font's ascent+descent is in ems (the UI sizes text by em: a font drawn at "15 px" should look 15 px
// across whichever font it is); 1 when it can't be read
float fontEmRatio(const fs::path& file);
// the player's text size (settings "textSize", percent, 75..150; 100 = as designed): every font is drawn this much bigger
float textScale(const json& settingsExtra);
// what the chosen fonts and size come to, to notice a change ("ui|title|size")
std::string fontChoiceKey(const json& settingsExtra);

} // namespace dq
