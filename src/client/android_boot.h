// client/android_boot.h — starting the game on Android (src/client/main.cpp calls it before App::run).
#pragma once
#include <string>

namespace dq {

// The game's files (data.dqa, data/resourcepacks, data/sounds, assets/) come inside the APK. The first start of each
// build copies them into the app's own storage, which becomes installDir(). The player's folder (userDir: saves,
// settings, resource packs, screenshots, logs) is the app's folder on the shared storage,
// Android/data/org.dokuquest.app/files, so a computer can reach it over USB. False with a reason when it can't start.
bool androidPrepare(std::string& error);

} // namespace dq
