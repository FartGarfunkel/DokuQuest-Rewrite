// ui/char_screens.h — the character creator and the skin editor, opened with something to start from
// (ui/screen.h has the plain factories the menus call).
#pragma once
#include "ui/screen.h"
#include "assets/char_creator.h"

namespace dq {

// the creator on a saved character ("" = the one you are wearing, else a fresh look)
std::unique_ptr<Screen> makeCreatorScreen(const std::string& character);
// the skin editor on a picture; character = "" saves to userDir()/skins/<name>.png, else back into that
// character's file (its look and body shape kept, the painted skin replaced)
std::unique_ptr<Screen> makeSkinEditorScreen(const SkinImage& start, const std::string& name, const std::string& character);

// the cape editor (ui/cape_screen.cpp): paint a 64x32 cape, preview it on you, save / wear / export / import it
std::unique_ptr<Screen> makeCapeEditorScreen();

// wear a skin reference: Settings::skin (saved) and the local player's look
void wearSkin(App& a, const std::string& ref);

} // namespace dq
