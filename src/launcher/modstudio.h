// launcher/modstudio.h — internals shared by the Mod Studio files (modstudio.cpp: editor, installed mods,
// data packs; modstudio_docs.cpp: overview, DQ API reference, engine source browser). The look follows the
// HTML Mod Studio (dokuquest-mod-studio.html): ember Jersey 15 headings, slot-coloured cards, gold card titles.
#pragma once
#include "launcher/launcher.h"

namespace dq::studio {

constexpr ImU32 S_PANEL = IM_COL32(27, 22, 18, 245), S_PANEL2 = IM_COL32(42, 34, 27, 255), S_SLOT = IM_COL32(34, 28, 23, 255),
                S_SLOTHI = IM_COL32(58, 48, 39, 255), S_LINE = IM_COL32(77, 63, 51, 255), S_LINEHI = IM_COL32(124, 102, 80, 255),
                S_TEXT = IM_COL32(239, 228, 204, 255), S_MUTED = IM_COL32(179, 164, 140, 255), S_EMBER = IM_COL32(255, 138, 61, 255),
                S_GOLD = IM_COL32(226, 184, 79, 255), S_FROST = IM_COL32(143, 227, 255, 255), S_OK = IM_COL32(155, 211, 74, 255), S_BAD = IM_COL32(255, 107, 94, 255), S_PRE = IM_COL32(18, 14, 11, 255);

void panelBegin(Launcher& L, const char* title);    // .panel + .panel-h
void panelEnd();
void cardBegin(Launcher& L, const char* title);     // .card + h3
void cardEnd();
void hint(Launcher& L, const char* text);           // .hint (muted, small)
void bullets(Launcher& L, std::initializer_list<const char*> items);   // .card ul (supports `code` spans)
void codeBlock(Launcher& L, const char* code);       // pre
bool button(Launcher& L, const char* label, bool primary = false, bool enabled = true);   // .btn / .btn.primary
void message(Launcher& L, const std::string& text, bool error);  // .msg / .msg.err
bool twoColumns(const char* id);                     // .grid2 (a table); NextCell between cards
void nextCell();
void endColumns();

struct Template { const char* id; const char* label; const char* code; };
const std::vector<Template>& templates();

void drawOverview(Launcher& L);
void drawApi(Launcher& L);
void drawEngine(Launcher& L);

} // namespace dq::studio
