// client/android_update.h — the phone build keeps itself up to date (android/java/.../MainActivity.java does the
// work): the title screen checks the newest Android build, offers it, downloads it and opens Android's installer.
// Nothing at all on a PC (the launcher updates the PC game).
#pragma once
#include <string>

namespace dq {

bool androidUpdateSupported();
void androidUpdateCheck();             // ask once (in the background)
// "" (checking, or nothing to say), "none", "available:<version>", "downloading:<percent>", "installing", "error:<why>"
std::string androidUpdateState();
void androidUpdateStart();             // download it and open the installer

} // namespace dq
