// ui/profiles.h — the four people of one screen (game.js loadProfiles / ui.js renderProfiles): each has a name, a
// skin (or a creator character) and a painted cape. Player 1's are the usual settings (playerName, skin, the worn
// cape); players 2-4 live in Settings::extra["profiles"][1..3]. Players & skins edits one of them by swapping it into
// the usual settings for as long as it's being edited, so the skin editor, creator and cape editor work for anyone.
#pragma once
#include <string>

namespace dq {
class App; struct Settings;

struct ProfileLook { std::string name, skin, cape; };
ProfileLook profileLook(Settings& s, int i);   // what player i (0..3) wears now (aware of an edit in progress)
void editProfile(Settings& s, int i);          // swap player i into the usual settings (0 = put player 1 back)
int editingProfile();
void applyProfiles(App& a);                    // dress everyone playing on this screen as their profile says

} // namespace dq
